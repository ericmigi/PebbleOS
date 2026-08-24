/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>

#include "applib/event_service_client.h"
#include "applib/tick_timer_service.h"

typedef struct __attribute__((packed)) TickTimerServiceState {
  TickHandler handler;
  TimeUnits tick_units;
  struct tm last_time;
  bool first_tick;
  bool last_is_24h;
  EventServiceInfo tick_service_info;
} TickTimerServiceState;

void tick_timer_service_state_init(TickTimerServiceState *state);
void tick_timer_service_init(void);
