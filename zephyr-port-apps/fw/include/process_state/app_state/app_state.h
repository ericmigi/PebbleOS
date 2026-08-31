/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "applib/event_service_client.h"
#include "applib/tick_timer_service_private.h"

typedef struct AnimationState AnimationState;

EventServiceInfo *app_state_get_event_service_state(void);
AnimationState *app_state_get_animation_state(void);
TickTimerServiceState *app_state_get_tick_timer_service_state(void);

// Per-app scratch pointer for the running privileged system app (system_app.c).
void app_state_set_user_data(void *data);
void *app_state_get_user_data(void);

// Provided by the fw button input service; click.c uses it in its serial command.
struct ClickManager;
struct ClickManager *app_state_get_click_manager(void);

// The port models a single visible window (launcher_ui.c), not a per-app
// WindowStack object; the status bar's only use of this passes the result to
// window_stack_is_animating_with_fixed_status_bar, which the port stubs false.
// Provided by fw/src/app_service_stubs.c.
typedef struct WindowStack WindowStack;
WindowStack *app_state_get_window_stack(void);
