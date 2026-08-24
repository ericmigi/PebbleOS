/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "pbl/os/tick.h"

#include <zephyr/kernel.h>

TickType_t milliseconds_to_ticks(uint32_t milliseconds) {
  return ((uint64_t)milliseconds * CONFIG_SYS_CLOCK_TICKS_PER_SEC) / 1000;
}

uint32_t ticks_to_milliseconds(TickType_t ticks) {
  return ((uint64_t)ticks * 1000) / CONFIG_SYS_CLOCK_TICKS_PER_SEC;
}
