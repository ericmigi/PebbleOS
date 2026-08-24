/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>

#include "pbl/util/uuid.h"
#include "process_management/app_install_types.h"

typedef enum {
  APP_AVAILABLE = 0,
  APP_REMOVED = 1,
  APP_ICON_NAME_UPDATED = 2,
  APP_UPGRADED = 3,
  APP_DB_CLEARED = 4,
} InstallEventType;

typedef void (*InstallCallbackDoneCallback)(void *data);

bool app_install_do_callbacks(InstallEventType event_type,
                              AppInstallId install_id, Uuid *uuid,
                              InstallCallbackDoneCallback done_callback,
                              void *done_callback_data);
void app_install_clear_app_db(void);
