/* SPDX-License-Identifier: Apache-2.0 */

// Phone -> watch PBW install over the port's Pebble Protocol path, mirroring
// shipping's app_install_manager / app_fetch_endpoint / put_bytes chain:
//   AppDB entries arrive over BlobDB (db 2) and land in the real app_db;
//   the launcher lists them; launching one whose binary is not on PFS asks
//   the phone (AppFetch, 0x1771) which streams app/resources/worker over
//   PutBytes into "@<id>/app", "@<id>/res", "@<id>/worker" (app_file naming).

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "app_registry.h"
#include "kernel/event_loop.h"
#include "kernel/pbl_malloc.h"
#include "process_management/pebble_process_info.h"
#include "pbl/services/blob_db/app_db.h"
#include "pbl/services/filesystem/pfs.h"
#include "pbl/util/uuid.h"
#include "system/status_codes.h"

#define APP_FETCH_ENDPOINT 0x1771
#define APP_FETCH_INSTALL_COMMAND 0x01

bool fw_pp_send(uint16_t endpoint, const uint8_t *payload, uint16_t len);
void fw_pbw_run(AppInstallId id);  // sandbox_launcher.c: run "@<id>/app" from PFS

static AppInstallId s_fetching;  // id we asked the phone for, INSTALL_ID_INVALID when idle
static AppInstallId s_installed;  // last id whose objects finished landing
static struct k_work_delayable s_install_done_work;

void fw_pbw_file_name(char *buf, size_t buf_len, AppInstallId id, const char *suffix) {
  // app_file_name_make(): '@' + 8 hex nybbles + '/' + suffix.
  static const char hex[] = "0123456789abcdef";
  uint32_t u = (uint32_t)id;
  if (buf_len < 11 + strlen(suffix)) {
    buf[0] = '\0';
    return;
  }
  buf[0] = '@';
  for (int i = 8; i >= 1; --i) {
    buf[i] = hex[u & 0xf];
    u >>= 4;
  }
  buf[9] = '/';
  strcpy(buf + 10, suffix);
}

bool fw_pbw_present(AppInstallId id) {
  char name[32];
  fw_pbw_file_name(name, sizeof(name), id, "app");
  const int fd = pfs_open(name, OP_FLAG_READ, FILE_TYPE_STATIC, 0);
  if (fd < 0) {
    return false;
  }
  pfs_close(fd);
  return true;
}

// Launch an AppDB app: run it if its binary is on PFS, else fetch it first.
void fw_pbw_launch(AppInstallId id) {
  if (fw_pbw_present(id)) {
    printk("PBW_LAUNCH %d\n", (int)id);
    fw_pbw_run(id);
    return;
  }
  AppDBEntry entry;
  if (app_db_get_app_entry_for_install_id(id, &entry) != S_SUCCESS) {
    printk("PBW_LAUNCH_FAIL %d no appdb entry\n", (int)id);
    return;
  }
  uint8_t req[1 + UUID_SIZE + 4];
  req[0] = APP_FETCH_INSTALL_COMMAND;
  memcpy(req + 1, &entry.uuid, UUID_SIZE);
  memcpy(req + 1 + UUID_SIZE, &id, sizeof(id));  // AppInstallId, native (LE) like shipping
  s_fetching = id;
  printk("APP_FETCH_REQ %d %s\n", (int)id, entry.name);
  (void)fw_pp_send(APP_FETCH_ENDPOINT, req, sizeof(req));
}

// Phone's answer to the fetch request: [0x01][result].
void fw_pbw_handle_fetch_response(const uint8_t *data, uint16_t len) {
  if (len < 2 || data[0] != 0x01) {
    return;
  }
  // 1 starting, 2 busy, 3 uuid invalid, 4 no data
  printk("APP_FETCH_RESP %u\n", data[1]);
  if (data[1] != 0x01) {
    s_fetching = INSTALL_ID_INVALID;
  }
}

static void prv_launch_cb(void *data) {
  fw_pbw_launch((AppInstallId)(intptr_t)data);
}

static void prv_install_done_work(struct k_work *work) {
  (void)work;
  extern bool putbytes_min_transfer_active(void) __attribute__((weak));
  if (putbytes_min_transfer_active && putbytes_min_transfer_active()) {
    (void)k_work_reschedule(&s_install_done_work, K_MSEC(700));  // resources still streaming
    return;
  }
  const AppInstallId id = s_installed;
  printk("PBW_INSTALLED %d\n", (int)id);
  if (id == s_fetching) {
    s_fetching = INSTALL_ID_INVALID;
    launcher_task_add_callback(prv_launch_cb, (void *)(intptr_t)id);
  }
}

// putbytes_min: an app object (app/res/worker) for `id` was committed and
// installed. The phone sends the objects back to back; treat a quiet gap as
// "all landed" (shipping waits for put_bytes to go idle the same way).
void fw_pbw_object_installed(AppInstallId id, uint8_t object_type) {
  printk("PBW_OBJECT %d type=%u\n", (int)id, object_type);
  s_installed = id;
  (void)k_work_reschedule(&s_install_done_work, K_MSEC(700));
}

// BlobDB (db 2) traffic from fw_pp_dispatch: keep the real app_db in step and
// refresh the launcher rows on KernelMain.
static void prv_registry_reload_cb(void *data) {
  (void)data;
  fw_app_registry_reload();
}

static void prv_registry_changed(void) {
  launcher_task_add_callback(prv_registry_reload_cb, NULL);
}

bool fw_pbw_appdb_insert(const uint8_t *key, int key_len, const uint8_t *val, int val_len) {
  const status_t rv = app_db_insert(key, key_len, val, val_len);
  if (val_len >= (int)sizeof(AppDBEntry)) {
    const AppDBEntry *e = (const AppDBEntry *)val;
    printk("APPDB_INSERT rv=%d \"%.*s\" flags=0x%08x icon=%u\n", (int)rv, APP_NAME_SIZE_BYTES,
           e->name, (unsigned)e->info_flags, (unsigned)e->icon_resource_id);
  } else {
    printk("APPDB_INSERT rv=%d len=%d\n", (int)rv, val_len);
  }
  if (rv == S_SUCCESS) {
    prv_registry_changed();
  }
  return rv == S_SUCCESS;
}

bool fw_pbw_appdb_delete(const uint8_t *key, int key_len) {
  const status_t rv = app_db_delete(key, key_len);
  printk("APPDB_DELETE rv=%d\n", (int)rv);
  if (rv == S_SUCCESS) {
    // Drop the binaries too (shipping's app_install_manager does this on removal).
    if (key_len == UUID_SIZE) {
      const AppInstallId id = app_db_get_install_id_for_uuid((const Uuid *)key);
      if (id != INSTALL_ID_INVALID) {
        char name[32];
        fw_pbw_file_name(name, sizeof(name), id, "app");
        pfs_remove(name);
        fw_pbw_file_name(name, sizeof(name), id, "res");
        pfs_remove(name);
        fw_pbw_file_name(name, sizeof(name), id, "worker");
        pfs_remove(name);
      }
    }
    prv_registry_changed();
  }
  return rv == S_SUCCESS;
}

bool fw_pbw_appdb_clear(void) {
  const status_t rv = app_db_flush();
  printk("APPDB_CLEAR rv=%d\n", (int)rv);
  prv_registry_changed();
  return rv == S_SUCCESS;
}

// APP_RUN_STATE run command from the phone (CoreApp "launch on watch").
static void prv_launch_uuid_cb(void *data) {
  AppInstallId id = app_db_get_install_id_for_uuid((const Uuid *)data);
  if (id == INSTALL_ID_INVALID) {
    // Not in AppDB (the phone's locker sync predates this firmware): create
    // a provisional entry so the fetch has an install id; the header
    // refresh after install fills in the real name and flags.
    AppDBEntry entry = { .uuid = *(const Uuid *)data };
    strncpy(entry.name, "Installing...", sizeof(entry.name) - 1);
    if (app_db_insert((const uint8_t *)data, UUID_SIZE, (const uint8_t *)&entry,
                      sizeof(entry)) == S_SUCCESS) {
      id = app_db_get_install_id_for_uuid((const Uuid *)data);
      fw_app_registry_reload();
    }
    printk("PBW_RUN_UUID provisional id=%d\n", (int)id);
  }
  kernel_free(data);
  if (id == INSTALL_ID_INVALID) {
    return;
  }
  const FwAppRegistryEntry *reg = fw_app_registry_find_by_id(id);
  if (reg && (reg->info_flags & PROCESS_INFO_WATCH_FACE)) {
    extern void watchface_set_default_install_id(AppInstallId app_id);
    extern void fw_request_watchface_switch(void);
    watchface_set_default_install_id(id);
    fw_request_watchface_switch();
    return;
  }
  fw_pbw_launch(id);
}

void fw_pbw_launch_uuid(const uint8_t uuid[16]) {
  Uuid *copy = kernel_malloc(sizeof(Uuid));
  if (!copy) {
    return;
  }
  memcpy(copy, uuid, sizeof(Uuid));
  launcher_task_add_callback(prv_launch_uuid_cb, copy);
}

// Until the PFS loader lands, running an installed PBW only reports.
__attribute__((weak)) void fw_pbw_run(AppInstallId id) {
  printk("PBW_RUN %d (loader not wired)\n", (int)id);
}

// After a load: make the AppDB entry match the PBW header (provisional
// entries, or a stale name/flags).
void fw_pbw_appdb_refresh_from_header(AppInstallId id, const char *name, uint32_t flags) {
  AppDBEntry entry;
  if (app_db_get_app_entry_for_install_id(id, &entry) != S_SUCCESS) {
    return;
  }
  const uint32_t new_flags = flags & (PROCESS_INFO_WATCH_FACE | PROCESS_INFO_VISIBILITY_HIDDEN |
                                      PROCESS_INFO_VISIBILITY_SHOWN_ON_COMMUNICATION);
  if (strncmp(entry.name, name, PROCESS_NAME_BYTES) == 0 && entry.info_flags == new_flags) {
    return;
  }
  strncpy(entry.name, name, PROCESS_NAME_BYTES);
  entry.name[PROCESS_NAME_BYTES] = '\0';
  entry.info_flags = new_flags;
  if (app_db_insert((const uint8_t *)&entry.uuid, UUID_SIZE, (const uint8_t *)&entry,
                    sizeof(entry)) == S_SUCCESS) {
    printk("APPDB_REFRESH %d \"%s\" flags=0x%x\n", (int)id, entry.name, (unsigned)new_flags);
    fw_app_registry_reload();
  }
}

void fw_pbw_install_init(void) {
  extern void putbytes_min_init(void);
  putbytes_min_init();
  s_fetching = INSTALL_ID_INVALID;
  k_work_init_delayable(&s_install_done_work, prv_install_done_work);
}
