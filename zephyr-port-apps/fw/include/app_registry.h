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
  // The launcher hands this to fw_system_app_launch() on SELECT. NULL entries
  // fall back to the sandboxed PBW launch.
  const PebbleProcessMd *md;
} FwAppRegistryEntry;

void fw_app_registry_init(void);
size_t fw_app_registry_count(void);
const FwAppRegistryEntry *fw_app_registry_get(size_t index);
const FwAppRegistryEntry *fw_launcher_pick_app(void);
const FwAppRegistryEntry *fw_app_registry_find_by_id(AppInstallId install_id);
