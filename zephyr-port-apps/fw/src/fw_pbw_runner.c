/* SPDX-License-Identifier: Apache-2.0 */

// Runs a phone-installed PBW the way the port runs its built-in system apps:
// loaded from PFS into the app segment, then launched through
// fw_system_app_launch on KernelMain with an md synthesised from the PBW
// header. The app's app_event_loop / window / click / timer calls resolve
// through the generated exported-symbol table to the same code the system
// apps use, so it rides the shell's window stack, pump and exit teardown.
// ponytail: privileged, no MPU isolation (the syscall-bridged sandbox in
// watchface_sandboxed/ stays for the embedded demo face); a misbehaving PBW
// can take the watch down. Isolation is the follow-up.

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/sys/printk.h>

#include "app_registry.h"
#include "process_management/pebble_process_info.h"
#include "process_management/pebble_process_md.h"
#include "resource/resource.h"
#include "pbl/util/uuid.h"

bool fw_pbw_load_file(int32_t id, PebbleProcessInfo *info_out, void **entry_out);
bool fw_pbw_present(AppInstallId id);
void fw_system_app_launch(const PebbleProcessMd *md);
void fw_shell_request_launch(const PebbleProcessMd *md);
void fw_system_app_request_exit(void);
void fw_request_watchface_switch(void);
void watchface_set_default_install_id(AppInstallId app_id);

static PebbleProcessMdSystem s_md;
static char s_md_name[PROCESS_NAME_BYTES + 1];
static AppInstallId s_loaded = INSTALL_ID_INVALID;   // id whose image is in the segment
static AppInstallId s_running = INSTALL_ID_INVALID;  // resource bank while its main runs
static int (*s_entry)(void);

ResAppNum fw_pbw_current_resource_num(void) {
  return s_running != INSTALL_ID_INVALID ? (ResAppNum)s_running : SYSTEM_APP;
}

const Uuid *fw_pbw_current_uuid(void) {
  return s_running != INSTALL_ID_INVALID ? &s_md.common.uuid : NULL;
}

static void prv_main(void) {
  const AppInstallId id = s_loaded;
  extern void persist_service_client_open(const Uuid *uuid);
  extern void persist_service_client_close(const Uuid *uuid);
  s_running = id;
  persist_service_client_open(&s_md.common.uuid);
  printk("PBW_MAIN %d\n", (int)id);
  const int rc = s_entry();
  printk("PBW_EXIT %d rc=%d\n", (int)id, rc);
  persist_service_client_close(&s_md.common.uuid);
  s_running = INSTALL_ID_INVALID;
}

// md for an installed PBW; loads it into the (single) app segment.
const PebbleProcessMd *fw_pbw_md_for(AppInstallId id) {
  PebbleProcessInfo info;
  void *entry;
  extern bool sandbox_arena_exec_enable(void);
  if (!sandbox_arena_exec_enable() || !fw_pbw_load_file(id, &info, &entry)) {
    s_loaded = INSTALL_ID_INVALID;
    return NULL;
  }
  s_entry = (int (*)(void))entry;
  s_loaded = id;
  memcpy(s_md_name, info.name, PROCESS_NAME_BYTES);
  s_md_name[PROCESS_NAME_BYTES] = '\0';
  s_md = (PebbleProcessMdSystem) {
    .common = {
      .main_func = prv_main,
      .process_type = (info.flags & PROCESS_INFO_WATCH_FACE) ? ProcessTypeWatchface : ProcessTypeApp,
      .visibility = (info.flags & PROCESS_INFO_VISIBILITY_HIDDEN) ? ProcessVisibilityHidden
                                                                  : ProcessVisibilityShown,
      .process_storage = ProcessStorageFlash,
      .is_unprivileged = false,
    },
    .name = s_md_name,
  };
  memcpy(&s_md.common.uuid, &info.uuid, sizeof(s_md.common.uuid));
  extern void fw_pbw_appdb_refresh_from_header(AppInstallId id, const char *name, uint32_t flags);
  fw_pbw_appdb_refresh_from_header(id, s_md_name, info.flags);
  return &s_md.common;
}

// fw_pbw_install.c: the binary is on PFS; run it.
void fw_pbw_run(AppInstallId id) {
  const PebbleProcessMd *md = fw_pbw_md_for(id);
  if (!md) {
    return;
  }
  if (md->process_type == ProcessTypeWatchface) {
    watchface_set_default_install_id(id);
    printk("WATCHFACE_SET %d\n", (int)id);
    fw_request_watchface_switch();
    return;
  }
  // Same hand-off the launcher uses for a built-in app (apps_port_glue.c).
  fw_shell_request_launch(md);
  fw_system_app_request_exit();
}
