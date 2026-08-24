/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <time.h>

#include "applib/event_service_client.h"

struct tm *sys_localtime_r(const time_t *timep, struct tm *result);
bool sys_app_is_watchface(void);
void sys_event_service_client_subscribe(EventServiceInfo *handler);
void sys_event_service_client_unsubscribe(EventServiceInfo *state, EventServiceInfo *handler);
void sys_event_service_cleanup(PebbleEvent *event);
