/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "kernel/events.h"
#include "pbl/util/list.h"

typedef void (*EventServiceEventHandler)(PebbleEvent *event, void *context);

typedef struct __attribute__((packed)) {
  ListNode list_node;
  PebbleEventType type;
  EventServiceEventHandler handler;
  void *context;
} EventServiceInfo;

void event_service_client_subscribe(EventServiceInfo *service_info);
void event_service_client_unsubscribe(EventServiceInfo *service_info);
