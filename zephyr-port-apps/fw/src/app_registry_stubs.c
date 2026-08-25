/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>

#include "pbl/services/app_fetch_endpoint.h"
#include "process_management/app_install_manager_private.h"

bool app_fetch_in_progress(void) {
  return false;
}

void app_fetch_cancel_from_system_task(AppInstallId app_id) {
  (void)app_id;
}

bool app_install_do_callbacks(InstallEventType event_type, AppInstallId install_id,
                              Uuid *uuid, InstallCallbackDoneCallback done_callback,
                              void *done_callback_data) {
  (void)event_type;
  (void)install_id;
  (void)uuid;
  (void)done_callback;
  (void)done_callback_data;
  return true;
}

void app_install_clear_app_db(void) {
}
