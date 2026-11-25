/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/util/bits.h>

// ESP32-S3 Core 0 interrupt matrix (base 0x600c2000).
// Source n maps to CPU interrupt line 0..31 at +4*n; line 16 is the reset/disabled default.
#define ESP32S3_INTMATRIX_MAP_OFFSET(n)    (0x0000U + 4U * (n))
#define ESP32S3_INTMATRIX_STATUS_OFFSET(i) (0x018cU + 4U * (i))
#define ESP32S3_INTMATRIX_CLOCK_GATE       0x019cU

#define ESP32S3_INTMATRIX_MAP_MASK PBL_GENMASK(4, 0)
