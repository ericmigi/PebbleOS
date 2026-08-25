/* SPDX-License-Identifier: Apache-2.0 */

#include "appdb_bootstrap.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/sys/printk.h>

#include "kernel/pebble_tasks.h"
#include "pbl/services/blob_db/app_db.h"
#include "pbl/services/filesystem/pfs.h"
#include "pbl/services/process_management/app_storage.h"
#include "process_management/pebble_process_info.h"
#include "sliding_text_emery_bin.h"
#include "system/status_codes.h"

static bool prv_code_bank_matches(const char *filename) {
  uint8_t buffer[128];
  size_t offset = 0;
  int fd = pfs_open(filename, OP_FLAG_READ, FILE_TYPE_STATIC, 0);
  if (fd < S_SUCCESS) {
    return false;
  }

  bool matches = pfs_get_file_size(fd) == sliding_text_emery_pbw_len;
  while (matches && offset < sliding_text_emery_pbw_len) {
    const size_t remaining = sliding_text_emery_pbw_len - offset;
    const size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
    const int bytes_read = pfs_read(fd, buffer, chunk);
    matches = bytes_read == (int)chunk &&
              memcmp(buffer, sliding_text_emery_pbw + offset, chunk) == 0;
    offset += chunk;
  }

  (void)pfs_close(fd);
  return matches;
}

static bool prv_install_code_bank(AppInstallId install_id) {
  char filename[APP_FILENAME_MAX_LENGTH];
  app_storage_get_file_name(filename, sizeof(filename), install_id,
                            PebbleTask_App);

  if (prv_code_bank_matches(filename)) {
    printk("FW_APP_PFS_READY id=%" PRId32 " file=%s bytes=%zu\n",
           install_id, filename, sliding_text_emery_pbw_len);
    return true;
  }

  (void)pfs_remove(filename);
  int fd = pfs_open(filename, OP_FLAG_WRITE, FILE_TYPE_STATIC,
                    sliding_text_emery_pbw_len);
  if (fd < S_SUCCESS) {
    printk("FW_APP_PFS_INSTALL_FAIL open=%d\n", fd);
    return false;
  }

  const int bytes_written =
      pfs_write(fd, sliding_text_emery_pbw, sliding_text_emery_pbw_len);
  const status_t close_result = pfs_close(fd);
  if (bytes_written != (int)sliding_text_emery_pbw_len ||
      close_result != S_SUCCESS || !prv_code_bank_matches(filename)) {
    printk("FW_APP_PFS_INSTALL_FAIL write=%d close=%" PRId32 "\n",
           bytes_written, close_result);
    return false;
  }

  printk("FW_APP_PFS_INSTALLED id=%" PRId32 " file=%s bytes=%zu\n",
         install_id, filename, sliding_text_emery_pbw_len);
  return true;
}

AppInstallId fw_appdb_install_test_app(void) {
  if (sliding_text_emery_pbw_len < sizeof(PebbleProcessInfo)) {
    printk("FW_APP_PFS_INSTALL_FAIL header\n");
    return INSTALL_ID_INVALID;
  }

  PebbleProcessInfo info;
  memcpy(&info, sliding_text_emery_pbw, sizeof(info));
  if (memcmp(info.header, "PBLAPP\0\0", sizeof(info.header)) != 0) {
    printk("FW_APP_PFS_INSTALL_FAIL format\n");
    return INSTALL_ID_INVALID;
  }

  Uuid uuid;
  memcpy(&uuid, &info.uuid, sizeof(uuid));
  AppInstallId install_id = app_db_get_install_id_for_uuid(&uuid);
  const bool needs_registration = install_id == INSTALL_ID_INVALID;
  if (needs_registration) {
    install_id = app_db_check_next_unique_id();
  }
  if (install_id == INSTALL_ID_INVALID || !prv_install_code_bank(install_id)) {
    return INSTALL_ID_INVALID;
  }

  if (needs_registration) {
    AppDBEntry entry = {
      .info_flags = info.flags,
      .icon_resource_id = info.icon_resource_id,
      .app_version = info.process_version,
      .sdk_version = info.sdk_version,
      .app_face_bg_color = { .argb = 0xc0 },
    };
    entry.uuid = uuid;
    strncpy(entry.name, info.name, sizeof(entry.name));
    entry.name[sizeof(entry.name) - 1] = '\0';

    const status_t result = app_db_insert((const uint8_t *)&entry.uuid,
                                          sizeof(entry.uuid),
                                          (const uint8_t *)&entry,
                                          sizeof(entry));
    if (result != S_SUCCESS) {
      printk("FW_APP_DB_REGISTER_FAIL %" PRId32 "\n", result);
      return INSTALL_ID_INVALID;
    }
    install_id = app_db_get_install_id_for_uuid(&entry.uuid);
    if (install_id == INSTALL_ID_INVALID) {
      printk("FW_APP_DB_REGISTER_FAIL lookup\n");
      return INSTALL_ID_INVALID;
    }
    printk("FW_APP_DB_REGISTERED id=%" PRId32 " name=%s\n", install_id,
           entry.name);
  } else {
    printk("FW_APP_DB_READY id=%" PRId32 " name=%s\n", install_id,
           info.name);
  }

  return install_id;
}
