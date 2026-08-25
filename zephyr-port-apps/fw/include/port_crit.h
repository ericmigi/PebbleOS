/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

// Map FreeRTOS-style critical sections (used by the shipping SiFli QSPI flash
// driver, src/fw/drivers/sf32lb52/qspi.c) onto Zephyr's IRQ lock. The flash
// erase/write path must run with interrupts masked so no ISR fetches from the
// XIP flash while the controller is in command mode. Not nesting-safe, which is
// fine: the flash driver enters/exits once per op and never re-enters.
#include <zephyr/irq.h>

extern unsigned int g_port_crit_key;

#define portENTER_CRITICAL() do { g_port_crit_key = irq_lock(); } while (0)
#define portEXIT_CRITICAL() do { irq_unlock(g_port_crit_key); } while (0)
