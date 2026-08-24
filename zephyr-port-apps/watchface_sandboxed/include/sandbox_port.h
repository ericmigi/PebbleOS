/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "applib/tick_timer_service.h"
#include "applib/ui/layer.h"

bool watchface_port_wait_tick(struct tm *tick_time, TimeUnits *units_changed,
                              TickHandler *handler, time_t *timestamp);
void watchface_port_render_tick(time_t timestamp);
int32_t watchface_port_time(int32_t *tloc);
struct tm *watchface_port_localtime(const int32_t *timep);
GRect watchface_layer_get_unobstructed_bounds_by_value(const Layer *layer);
