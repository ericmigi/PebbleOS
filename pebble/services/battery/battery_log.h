/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

//! Record a battery sample in the discharge log, at most once a minute.
void battery_log_sample(uint16_t millivolts, uint8_t percent, bool usb);

#if UNITTEST
void battery_log_reset_for_tests(void);
#endif
