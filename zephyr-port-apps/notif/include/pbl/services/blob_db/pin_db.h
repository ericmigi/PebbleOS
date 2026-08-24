/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdint.h>

#include "pbl/services/timeline/item.h"

typedef int32_t status_t;

#define S_SUCCESS 0

status_t pin_db_get(const TimelineItemId *id, TimelineItem *pin);

