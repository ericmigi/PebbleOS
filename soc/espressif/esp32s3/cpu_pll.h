/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>

// Prepare the core supply for 240 MHz when the ROM has a working 480 MHz PLL.
bool esp32s3_prepare_cpu_pll(void);
