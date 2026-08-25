/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "process_management/app_install_types.h"
#include "process_management/pebble_process_md.h"
#include "pbl/util/uuid.h"

#define FW_APP_NAME_SIZE 96

typedef struct {
  AppInstallId install_id;
  Uuid uuid;
  uint32_t info_flags;
  bool installed;
  char name[FW_APP_NAME_SIZE + 1];
  // Non-NULL for a privileged built-in system app: its real PebbleProcessMd.
  // The launcher hands this to fw_system_app_launch() on SELECT. Installed
  // NULL-md entries launch by AppInstallId; unported system entries are inert.
  const PebbleProcessMd *md;
} FwAppRegistryEntry;

// Always registers the static system apps. When appdb_available is true (PFS
// mounted), it additionally best-effort installs + enumerates AppDB apps; an
// AppDB failure never empties or gates the static list.
void fw_app_registry_init(bool appdb_available);
size_t fw_app_registry_count(void);
const FwAppRegistryEntry *fw_app_registry_get(size_t index);
const FwAppRegistryEntry *fw_launcher_pick_app(void);
