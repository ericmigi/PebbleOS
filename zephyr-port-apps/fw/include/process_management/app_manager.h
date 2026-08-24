/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "kernel/events.h"

// Only reached for App/Worker-targeted timers; the port drives KernelMain only.
bool process_manager_send_event_to_process(PebbleTask task, PebbleEvent *e);
