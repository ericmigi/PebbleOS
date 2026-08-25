/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "applib/event_service_client.h"
#include "applib/tick_timer_service_private.h"

EventServiceInfo *app_state_get_event_service_state(void);
TickTimerServiceState *app_state_get_tick_timer_service_state(void);

// Per-app scratch pointer for the running privileged system app (system_app.c).
void app_state_set_user_data(void *data);
void *app_state_get_user_data(void);

// Provided by the fw button input service; click.c uses it in its serial command.
struct ClickManager;
struct ClickManager *app_state_get_click_manager(void);
