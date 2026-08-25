/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>

#include "kernel/events.h"
#include "kernel/util/segment.h"
#include "process_management/app_install_types.h"

// Only reached for App/Worker-targeted timers; the port drives KernelMain only.
bool process_manager_send_event_to_process(PebbleTask task, PebbleEvent *e);

//! Zephyr core-boot entry corresponding to app_manager's code-bank portion of
//! prv_app_start(): resolve flash metadata, then invoke process_loader_load().
void *app_manager_load_code_bank(AppInstallId install_id,
                                 MemorySegment *destination,
                                 size_t *loaded_size_out);
