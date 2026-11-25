/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/services/battery/battery_curve.h>

#include <clar.h>

// The Xteink X4's curve, CrossPoint's notches: percent from millivolts, discharging
static int prv_percent(int mv) {
  return battery_curve_lookup_percent_with_scaling_factor(mv, false, 1);
}

// To the nearest 10 %, as the reported percent is rounded
static int prv_notch(int mv) {
  return ((prv_percent(mv) + 5) / 10) * 10;
}

void test_battery_curve_xteinkx4__empty_at_3450(void) {
  cl_assert_equal_i(prv_percent(0), 0);
  cl_assert_equal_i(prv_percent(3300), 0);
  cl_assert_equal_i(prv_percent(3450), 0);
}

void test_battery_curve_xteinkx4__notches(void) {
  cl_assert_equal_i(prv_percent(3680), 10);
  cl_assert_equal_i(prv_percent(3740), 20);
  cl_assert_equal_i(prv_percent(3820), 50);
  cl_assert_equal_i(prv_percent(3980), 80);
  cl_assert_equal_i(prv_percent(4200), 100);
  cl_assert_equal_i(prv_percent(4350), 100);
}

void test_battery_curve_xteinkx4__rounds_at_segment_midpoints(void) {
  // 3680-3740 mV is 10-20 %: below its midpoint 10 %, from it 20 %
  cl_assert_equal_i(prv_notch(3709), 10);
  cl_assert_equal_i(prv_notch(3710), 20);
  // 4060-4200 mV is 90-100 %: a cell just off the charger reads full
  cl_assert_equal_i(prv_notch(4129), 90);
  cl_assert_equal_i(prv_notch(4130), 100);
  // 3450-3680 mV is 0-10 %
  cl_assert_equal_i(prv_notch(3564), 0);
  cl_assert_equal_i(prv_notch(3565), 10);
}
