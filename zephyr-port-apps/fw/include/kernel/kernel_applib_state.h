/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "applib/event_service_client.h"
#include "applib/tick_timer_service_private.h"

EventServiceInfo *kernel_applib_get_event_service_state(void);
TickTimerServiceState *kernel_applib_get_tick_timer_service_state(void);
