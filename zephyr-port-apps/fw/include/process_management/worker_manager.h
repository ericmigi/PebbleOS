/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

// Port shadow: the qemu shell has no worker task; the Background App screen
// renders the reference's "No background apps" state.

#include <stdint.h>

#include "process_management/app_install_manager.h"
#include "process_management/app_manager.h"
#include "process_management/process_manager.h"

AppInstallId worker_manager_get_current_worker_id(void);
ProcessContext *worker_manager_get_task_context(void);
void worker_manager_put_launch_worker_event(AppInstallId id);
void worker_manager_set_default_install_id(AppInstallId id);
