/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#define BLE_NPL_OS_ALIGNMENT 4
#define BLE_NPL_TIME_FOREVER UINT32_MAX

typedef uint32_t ble_npl_time_t;
typedef int32_t ble_npl_stime_t;

struct ble_npl_event {
  void *queue_link;
  atomic_t queued;
  ble_npl_event_fn *fn;
  void *arg;
};

struct ble_npl_eventq {
  struct k_queue queue;
};

struct ble_npl_callout {
  struct k_work_delayable work;
  struct ble_npl_eventq *evq;
  struct ble_npl_event ev;
  ble_npl_time_t ticks;
  atomic_t active;
};

struct ble_npl_mutex {
  struct k_mutex mutex;
};

struct ble_npl_sem {
  struct k_sem sem;
};
