/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "kernel/events.h"

typedef void (*EventServiceAddSubscriberCallback)(PebbleTask task);
typedef void (*EventServiceRemoveSubscriberCallback)(PebbleTask task);

void event_service_init(PebbleEventType type, EventServiceAddSubscriberCallback add_subscriber,
                        EventServiceRemoveSubscriberCallback remove_subscriber);
