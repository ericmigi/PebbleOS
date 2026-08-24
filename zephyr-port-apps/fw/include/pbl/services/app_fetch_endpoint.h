/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>

#include "process_management/app_install_types.h"

void app_fetch_cancel_from_system_task(AppInstallId app_id);
bool app_fetch_in_progress(void);
