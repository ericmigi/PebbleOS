/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#include <pbl/util/bits.h>

#include <hardware/esp32c3_systimer.h>

// Never enter light sleep during the first 30 s after boot, keeping recovery easy
#define ESP32C3_IDLE_BOOT_HOLD_S     30U
#define ESP32C3_IDLE_BOOT_HOLD_TICKS ((uint64_t)ESP32C3_IDLE_BOOT_HOLD_S * ESP32C3_SYSTIMER_HZ)
// Shortest SYSTIMER window left (after exit latency) worth sleeping for: 3 ms
#define ESP32C3_IDLE_MIN_SLEEP_TICKS (3ULL * (ESP32C3_SYSTIMER_HZ / 1000U))
// The main crystal frequency, used to calibrate the slow RC clock
#define ESP32C3_IDLE_XTAL_HZ 40000000U
// Width of the RTC timer counter
#define ESP32C3_IDLE_RTC_MASK ((1ULL << 48) - 1U)

enum esp32c3_idle_blocker : uint8_t {
  ESP32C3_IDLE_BLOCK_NONE = 0U,
  ESP32C3_IDLE_BLOCK_USB = PBL_BIT(0),
  ESP32C3_IDLE_BLOCK_BOOT_HOLD = PBL_BIT(1),
  ESP32C3_IDLE_BLOCK_RTC_WDT = PBL_BIT(2),
  ESP32C3_IDLE_BLOCK_UNCALIBRATED = PBL_BIT(3),
  ESP32C3_IDLE_BLOCK_SHORT_WINDOW = PBL_BIT(4),
};

// Active reasons refusing light sleep right now; 0 means light sleep is allowed
static inline uint8_t esp32c3_idle_blockers(uint64_t now, uint64_t target, bool usb,
                                            bool rtc_wdt_armed, bool calibrated) {
  uint8_t blockers = ESP32C3_IDLE_BLOCK_NONE;

  if (usb) {
    blockers |= ESP32C3_IDLE_BLOCK_USB;
  }
  if (now < ESP32C3_IDLE_BOOT_HOLD_TICKS) {
    blockers |= ESP32C3_IDLE_BLOCK_BOOT_HOLD;
  }
  if (rtc_wdt_armed) {
    blockers |= ESP32C3_IDLE_BLOCK_RTC_WDT;
  }
  if (!calibrated) {
    blockers |= ESP32C3_IDLE_BLOCK_UNCALIBRATED;
  }
  if (target <= now || (target - now) < ESP32C3_IDLE_MIN_SLEEP_TICKS) {
    blockers |= ESP32C3_IDLE_BLOCK_SHORT_WINDOW;
  }

  return blockers;
}

// SYSTIMER ticks to sleep for, or 0 if any hardware safety gate refuses sleep
static inline uint64_t esp32c3_idle_gate(uint64_t now, uint64_t target, bool usb,
                                         bool rtc_wdt_armed, bool calibrated, uint64_t max_ticks) {
  if (max_ticks == 0U || esp32c3_idle_blockers(now, target, usb, rtc_wdt_armed, calibrated) != 0U) {
    return 0U;
  }

  const uint64_t delta = target - now;

  return (delta > max_ticks) ? max_ticks : delta;
}

// SYSTIMER ticks per slow clock cycle in 16.16 fixed point, 0 if out of range (40..400 kHz)
static inline uint32_t esp32c3_idle_cal_to_period(uint32_t xtal_cycles, uint32_t cal_cycles) {
  static_assert(ESP32C3_SYSTIMER_HZ * 5ULL == ESP32C3_IDLE_XTAL_HZ * 2ULL,
                "SYSTIMER / XTAL ratio must be 2 / 5");
  if (cal_cycles == 0U || (uint64_t)xtal_cycles < (uint64_t)cal_cycles * 100U ||
      (uint64_t)xtal_cycles > (uint64_t)cal_cycles * 1000U) {
    return 0U;
  }

  return (uint32_t)((((uint64_t)xtal_cycles * 2ULL) << 16) / (5ULL * cal_cycles));
}

// Convert a SYSTIMER sleep duration into RTC slow clock ticks
static inline uint64_t esp32c3_idle_ticks_to_rtc(uint64_t sys_ticks, uint32_t slow_period) {
  if (slow_period == 0U) {
    return 0U;
  }

  return (sys_ticks << 16) / slow_period;
}

// Convert elapsed 48-bit RTC slow clock ticks back into SYSTIMER ticks
static inline uint64_t esp32c3_idle_rtc_to_ticks(uint64_t rtc_start, uint64_t rtc_end,
                                                 uint32_t slow_period) {
  const uint64_t elapsed = (rtc_end - rtc_start) & ESP32C3_IDLE_RTC_MASK;

  return (elapsed * slow_period) >> 16;
}
