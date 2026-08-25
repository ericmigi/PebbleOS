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

// Registers the static system apps synchronously; records whether the AppDB is
// available (PFS mounted) for the deferred load below. Never touches PFS/AppDB
// itself, so it cannot block boot.
void fw_app_registry_init(bool appdb_available);

// Best-effort AppDB install + enumerate. Must run only AFTER the launcher is up
// (deferred onto the launcher loop) so a slow/wedged PFS op can't gate boot.
// Returns true if it added installed apps (caller should reload the menu).
bool fw_app_registry_load_appdb(void);
size_t fw_app_registry_count(void);
const FwAppRegistryEntry *fw_app_registry_get(size_t index);
const FwAppRegistryEntry *fw_launcher_pick_app(void);
