/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "applib/tick_timer_service_private.h"

typedef struct Layer Layer;

TickTimerServiceState *kernel_applib_get_tick_timer_service_state(void);
Layer **kernel_applib_get_layer_tree_stack(void);

