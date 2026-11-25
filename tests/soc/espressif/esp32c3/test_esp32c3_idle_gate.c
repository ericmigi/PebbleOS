/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>

#include <clar.h>
#include <idle_gate.h>

#define MAX_TICKS    (10ULL * ESP32C3_SYSTIMER_HZ)
#define AFTER_BOOT   (ESP32C3_IDLE_BOOT_HOLD_TICKS + ESP32C3_SYSTIMER_HZ)
#define MS_TICKS(ms) ((uint64_t)(ms) * (ESP32C3_SYSTIMER_HZ / 1000U))

void test_esp32c3_idle_gate__refuses_usb(void) {
  const uint8_t b =
      esp32c3_idle_blockers(AFTER_BOOT, AFTER_BOOT + MS_TICKS(500), true, false, true);

  cl_assert_equal_i(b, ESP32C3_IDLE_BLOCK_USB);
  cl_assert_equal_i(
      esp32c3_idle_gate(AFTER_BOOT, AFTER_BOOT + MS_TICKS(500), true, false, true, MAX_TICKS), 0U);
}

void test_esp32c3_idle_gate__refuses_first_30s(void) {
  // At 0 s and 1 tick before 30 s: refused
  cl_assert_equal_i(esp32c3_idle_blockers(0U, MS_TICKS(500), false, false, true),
                    ESP32C3_IDLE_BLOCK_BOOT_HOLD);
  cl_assert_equal_i(esp32c3_idle_gate(ESP32C3_IDLE_BOOT_HOLD_TICKS - 1U,
                                      ESP32C3_IDLE_BOOT_HOLD_TICKS + MS_TICKS(500), false, false,
                                      true, MAX_TICKS),
                    0U);

  // At exactly 30 s: allowed
  cl_assert_equal_i(
      esp32c3_idle_blockers(ESP32C3_IDLE_BOOT_HOLD_TICKS,
                            ESP32C3_IDLE_BOOT_HOLD_TICKS + MS_TICKS(500), false, false, true),
      ESP32C3_IDLE_BLOCK_NONE);
  cl_assert_equal_i(
      esp32c3_idle_gate(ESP32C3_IDLE_BOOT_HOLD_TICKS, ESP32C3_IDLE_BOOT_HOLD_TICKS + MS_TICKS(500),
                        false, false, true, MAX_TICKS),
      MS_TICKS(500));
}

void test_esp32c3_idle_gate__refuses_rtc_wdt(void) {
  cl_assert_equal_i(
      esp32c3_idle_blockers(AFTER_BOOT, AFTER_BOOT + MS_TICKS(500), false, true, true),
      ESP32C3_IDLE_BLOCK_RTC_WDT);
  cl_assert_equal_i(
      esp32c3_idle_gate(AFTER_BOOT, AFTER_BOOT + MS_TICKS(500), false, true, true, MAX_TICKS), 0U);
}

void test_esp32c3_idle_gate__refuses_uncalibrated(void) {
  cl_assert_equal_i(
      esp32c3_idle_blockers(AFTER_BOOT, AFTER_BOOT + MS_TICKS(500), false, false, false),
      ESP32C3_IDLE_BLOCK_UNCALIBRATED);
  cl_assert_equal_i(
      esp32c3_idle_gate(AFTER_BOOT, AFTER_BOOT + MS_TICKS(500), false, false, false, MAX_TICKS),
      0U);
}

void test_esp32c3_idle_gate__refuses_short_or_past_target(void) {
  // Target in the past or equal to now
  cl_assert_equal_i(esp32c3_idle_blockers(AFTER_BOOT, AFTER_BOOT, false, false, true),
                    ESP32C3_IDLE_BLOCK_SHORT_WINDOW);
  cl_assert_equal_i(esp32c3_idle_gate(AFTER_BOOT, AFTER_BOOT - 10U, false, false, true, MAX_TICKS),
                    0U);

  // Just under minimum sleep window
  cl_assert_equal_i(esp32c3_idle_gate(AFTER_BOOT, AFTER_BOOT + ESP32C3_IDLE_MIN_SLEEP_TICKS - 1U,
                                      false, false, true, MAX_TICKS),
                    0U);

  // At exact minimum sleep window: allowed
  cl_assert_equal_i(esp32c3_idle_gate(AFTER_BOOT, AFTER_BOOT + ESP32C3_IDLE_MIN_SLEEP_TICKS, false,
                                      false, true, MAX_TICKS),
                    ESP32C3_IDLE_MIN_SLEEP_TICKS);
}

void test_esp32c3_idle_gate__reports_multiple_blockers(void) {
  const uint8_t b = esp32c3_idle_blockers(0U, 0U, true, true, false);

  cl_assert_equal_i(b, ESP32C3_IDLE_BLOCK_USB | ESP32C3_IDLE_BLOCK_BOOT_HOLD |
                           ESP32C3_IDLE_BLOCK_RTC_WDT | ESP32C3_IDLE_BLOCK_UNCALIBRATED |
                           ESP32C3_IDLE_BLOCK_SHORT_WINDOW);
}

void test_esp32c3_idle_gate__caps_at_max_ticks(void) {
  // Target 60 s out -> capped at 10 s
  cl_assert_equal_i(
      esp32c3_idle_gate(AFTER_BOOT, AFTER_BOOT + MS_TICKS(60000), false, false, true, MAX_TICKS),
      MAX_TICKS);

  // Unarmed comparator (UINT64_MAX) -> capped at 10 s
  cl_assert_equal_i(esp32c3_idle_gate(AFTER_BOOT, UINT64_MAX, false, false, true, MAX_TICKS),
                    MAX_TICKS);
}

void test_esp32c3_idle_gate__slow_clock_math(void) {
  // Nominal 136 kHz slow clock over 1024 cycles: ~301176 XTAL cycles at 40 MHz
  const uint32_t period = esp32c3_idle_cal_to_period(301176U, 1024U);

  cl_assert(period > 0U);
  // Out-of-range calibration rejected
  cl_assert_equal_i(esp32c3_idle_cal_to_period(0U, 1024U), 0U);
  cl_assert_equal_i(esp32c3_idle_cal_to_period(1000U, 1024U), 0U);
  cl_assert_equal_i(esp32c3_idle_cal_to_period(2000000U, 1024U), 0U);

  // Large cycle counts do not overflow uint64_t
  cl_assert_equal_i(esp32c3_idle_cal_to_period(300000000U, 1000000U), 120U << 16);

  // Round-trip 10 s sleep within 1 slow clock tick (~118 SYSTIMER ticks)
  const uint64_t rtc_ticks = esp32c3_idle_ticks_to_rtc(MAX_TICKS, period);
  const uint64_t back = esp32c3_idle_rtc_to_ticks(0U, rtc_ticks, period);

  cl_assert(back <= MAX_TICKS);
  cl_assert(MAX_TICKS - back < (period >> 16) + 2U);

  // 48-bit RTC counter wraparound
  const uint64_t start = ESP32C3_IDLE_RTC_MASK - 50U;
  const uint64_t end = (start + rtc_ticks) & ESP32C3_IDLE_RTC_MASK;

  cl_assert_equal_i(esp32c3_idle_rtc_to_ticks(start, end, period), back);
}
