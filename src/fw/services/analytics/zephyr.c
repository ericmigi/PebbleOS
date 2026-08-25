/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#ifdef CONFIG_PEBBLE_ZEPHYR_CORE_BOOT

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <pbl/drivers/rtc.h>
#include "pbl/os/mutex.h"
#include "pbl/services/analytics/backend.h"
#include "system/passert.h"

#define ANALYTICS_STRING_MAX_LEN 64U

static PebbleMutex *s_mutex;
static int32_t s_values[PBL_ANALYTICS_KEY_COUNT];
static RtcTicks s_timer_started[PBL_ANALYTICS_KEY_COUNT];
static bool s_timer_running[PBL_ANALYTICS_KEY_COUNT];
static char s_strings[PBL_ANALYTICS_KEY_COUNT][ANALYTICS_STRING_MAX_LEN + 1U];

void pbl_analytics__zephyr_init(void) {
  s_mutex = mutex_create();
  PBL_ASSERTN(s_mutex != NULL);
}

void pbl_analytics__zephyr_heartbeat(void) {
  // ponytail: metrics are recorded in RAM, but pt2 has no Memfault transport
  // or native DLS upload path yet. Wire either transport here, then snapshot
  // and clear the interval values as native.c does for shipping firmware.
}

static void prv_set_signed(enum pbl_analytics_key key, int32_t value) {
  mutex_lock(s_mutex);
  s_values[key] = value;
  mutex_unlock(s_mutex);
}

static void prv_set_unsigned(enum pbl_analytics_key key, uint32_t value) {
  prv_set_signed(key, (int32_t)value);
}

static void prv_set_string(enum pbl_analytics_key key, const char *value) {
  mutex_lock(s_mutex);
  strncpy(s_strings[key], value, ANALYTICS_STRING_MAX_LEN);
  s_strings[key][ANALYTICS_STRING_MAX_LEN] = '\0';
  mutex_unlock(s_mutex);
}

static void prv_timer_start(enum pbl_analytics_key key) {
  mutex_lock(s_mutex);
  if (!s_timer_running[key]) {
    s_timer_running[key] = true;
    s_timer_started[key] = rtc_get_ticks();
  }
  mutex_unlock(s_mutex);
}

static void prv_timer_stop(enum pbl_analytics_key key) {
  mutex_lock(s_mutex);
  if (s_timer_running[key]) {
    const RtcTicks elapsed = rtc_get_ticks() - s_timer_started[key];
    s_values[key] += (int32_t)((elapsed * 1000U) / RTC_TICKS_HZ);
    s_timer_running[key] = false;
  }
  mutex_unlock(s_mutex);
}

static void prv_add(enum pbl_analytics_key key, int32_t amount) {
  mutex_lock(s_mutex);
  s_values[key] += amount;
  mutex_unlock(s_mutex);
}

const struct pbl_analytics_backend_ops pbl_analytics__zephyr_ops = {
    .set_signed = prv_set_signed,
    .set_unsigned = prv_set_unsigned,
    .set_string = prv_set_string,
    .timer_start = prv_timer_start,
    .timer_stop = prv_timer_stop,
    .add = prv_add,
};

#endif  // CONFIG_PEBBLE_ZEPHYR_CORE_BOOT
