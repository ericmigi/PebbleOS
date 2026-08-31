/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

// Force-included ahead of the real applib animation engine + animation service
// TUs. Supplies the Zephyr sign_extend dance, FreeRTOS-style critical sections
// (single-pump port: IRQ lock), the legacy2 header's expected types, and the
// scheduler-dump serial sink.
#include "fw_zephyr_pre.h"
#include "port_crit.h"

#include "applib/app_timer.h"

typedef enum AppTaskCtxIdx {
  AppTaskCtxIdxLauncher = 0,
  AppTaskCtxIdxApp,
  AppTaskCtxIdxCount,
} AppTaskCtxIdx;

#include <zephyr/sys/printk.h>
#define dbgserial_putstr_fmt(buffer, size, fmt, ...) \
  do { snprintk((buffer), (size), fmt, ##__VA_ARGS__); printk("%s\n", (buffer)); } while (0)

#include "process_management/process_manager.h"
