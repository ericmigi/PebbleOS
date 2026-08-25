/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#ifdef CONFIG_PEBBLE_ZEPHYR_CORE_BOOT

#include <zephyr/kernel.h>

#include <pbl/drivers/task_watchdog.h>
#include <pbl/drivers/watchdog.h>

#include "kernel/pebble_tasks.h"

#define DEFAULT_TASK_WATCHDOG_MASK (1U << PebbleTask_NewTimers)

static struct k_timer s_feed_timer;
static struct k_spinlock s_lock;
static PebbleTaskBitset s_watchdog_bits;
static PebbleTaskBitset s_watchdog_mask = DEFAULT_TASK_WATCHDOG_MASK;
static uint32_t s_pause_ticks_remaining;

static void prv_feed_if_healthy(void) {
  bool should_feed = false;
  k_spinlock_key_t key = k_spin_lock(&s_lock);

  if (s_pause_ticks_remaining > 0U) {
    --s_pause_ticks_remaining;
    should_feed = true;
  } else if ((s_watchdog_bits & s_watchdog_mask) == s_watchdog_mask) {
    s_watchdog_bits = 0U;
    should_feed = true;
  }

  k_spin_unlock(&s_lock, key);
  if (should_feed) {
    watchdog_feed();
  }
}

static void prv_feed_timer_expired(struct k_timer *timer) {
  ARG_UNUSED(timer);
  prv_feed_if_healthy();
}

void task_watchdog_init(void) {
  watchdog_init();
  watchdog_start();
  k_timer_init(&s_feed_timer, prv_feed_timer_expired, NULL);
  k_timer_start(&s_feed_timer, K_MSEC(TASK_WATCHDOG_FEED_PERIOD_MS),
                K_MSEC(TASK_WATCHDOG_FEED_PERIOD_MS));
}

void task_watchdog_feed(void) {
  prv_feed_if_healthy();
}

void task_watchdog_pause(unsigned int seconds) {
  k_spinlock_key_t key = k_spin_lock(&s_lock);
  s_pause_ticks_remaining =
      (seconds * 1000U + TASK_WATCHDOG_FEED_PERIOD_MS - 1U) /
      TASK_WATCHDOG_FEED_PERIOD_MS;
  k_spin_unlock(&s_lock, key);
}

void task_watchdog_resume(void) {
  k_spinlock_key_t key = k_spin_lock(&s_lock);
  s_pause_ticks_remaining = 0U;
  k_spin_unlock(&s_lock, key);
}

void task_watchdog_bit_set(PebbleTask task) {
  k_spinlock_key_t key = k_spin_lock(&s_lock);
  s_watchdog_bits |= 1U << task;
  k_spin_unlock(&s_lock, key);
}

void task_watchdog_bit_set_all(void) {
  k_spinlock_key_t key = k_spin_lock(&s_lock);
  s_watchdog_bits |= s_watchdog_mask;
  k_spin_unlock(&s_lock, key);
}

bool task_watchdog_mask_get(PebbleTask task) {
  k_spinlock_key_t key = k_spin_lock(&s_lock);
  const bool is_set = (s_watchdog_mask & (1U << task)) != 0U;
  k_spin_unlock(&s_lock, key);
  return is_set;
}

void task_watchdog_mask_set(PebbleTask task) {
  k_spinlock_key_t key = k_spin_lock(&s_lock);
  s_watchdog_mask |= 1U << task;
  k_spin_unlock(&s_lock, key);
}

void task_watchdog_mask_clear(PebbleTask task) {
  k_spinlock_key_t key = k_spin_lock(&s_lock);
  s_watchdog_mask &= ~(1U << task);
  k_spin_unlock(&s_lock, key);
}

void task_watchdog_step_elapsed_time_ms(uint32_t elapsed_ms) {
  ARG_UNUSED(elapsed_ms);
  prv_feed_if_healthy();
}

#endif  // CONFIG_PEBBLE_ZEPHYR_CORE_BOOT
