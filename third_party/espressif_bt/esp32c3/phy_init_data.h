/*
 * SPDX-FileCopyrightText: 2016-2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2026 Core Devices LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// What phy_init_data.c, taken unchanged from ESP-IDF 5.5.5, needs from
// components/esp_phy/esp32c3/include/phy_init_data.h.

#pragma once

#include "phy.h"

// constrain a value between 'low' and 'high', inclusive
#define LIMIT(val, low, high) ((val < low) ? low : (val > high) ? high : val)

#define PHY_INIT_MAGIC "PHYINIT"

extern const char phy_init_magic_pre[];
extern const esp_phy_init_data_t phy_init_data;
extern const char phy_init_magic_post[];
