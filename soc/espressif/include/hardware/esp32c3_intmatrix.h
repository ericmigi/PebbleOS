/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/util/bits.h>

// Source n is routed to the CPU line in its map register; line 0 is no line
#define ESP32C3_INTMATRIX_MAP_OFFSET(n)    (0x0000 + 4U * (n))
#define ESP32C3_INTMATRIX_STATUS_OFFSET(i) (0x00f8 + 4U * (i))
#define ESP32C3_INTMATRIX_ENABLE_OFFSET    0x0104
#define ESP32C3_INTMATRIX_TYPE_OFFSET      0x0108
#define ESP32C3_INTMATRIX_CLEAR_OFFSET     0x010c
#define ESP32C3_INTMATRIX_PRI_OFFSET(line) (0x0114 + 4U * (line))
#define ESP32C3_INTMATRIX_THRESH_OFFSET    0x0194

#define INTMATRIX_MAP_MASK PBL_GENMASK(4, 0)
#define INTMATRIX_PRI_MASK PBL_GENMASK(3, 0)
