/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

// Port shadow of process_management/process_manager.h (fw/include is first on
// the include path). The apps' applib UI (status bar, action bar) only need the
// current-platform query and the legacy2-SDK flag; the port runs a single
// privileged process, so these are constant.

#include <stdbool.h>

#include "applib/platform.h"

PlatformType process_manager_current_platform(void);

bool process_manager_compiled_with_legacy2_sdk(void);

// Deferred-callback post used by the real launcher's menu layer; the port runs
// apps on the shared KernelMain pump, so it lands on the shared event queue
// (fw_shell.c).
#include "kernel/pebble_tasks.h"
void process_manager_send_callback_event_to_process(PebbleTask task, void (*callback)(void *),
                                                    void *data);
