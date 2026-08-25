/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#ifdef CONFIG_PEBBLE_ZEPHYR_CORE_BOOT

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/util.h>

#include "pbl/logging/logging.h"
#include "system/passert.h"

void board_early_init(void);
void board_init(void);

static const struct device *const s_i2c_buses[] = {
    DEVICE_DT_GET(DT_NODELABEL(i2c1)),
    DEVICE_DT_GET(DT_NODELABEL(i2c2)),
    DEVICE_DT_GET(DT_NODELABEL(i2c3)),
    DEVICE_DT_GET(DT_NODELABEL(i2c4)),
};

void board_early_init(void) {
  // Obelix's shipping board_early_init() is intentionally empty. Zephyr has
  // already initialized clocks and pinctrl before entering the application.
}

void board_init(void) {
  // Shipping board_init() initializes these four buses before any attached
  // peripheral. Under Zephyr, DEVICE_DT_GET() binds the same Obelix buses and
  // their POST_KERNEL init has already completed; asserting readiness here
  // preserves board_init() as the boundary at which clients may start I/O.
  for (size_t i = 0; i < ARRAY_SIZE(s_i2c_buses); ++i) {
    PBL_ASSERTN(device_is_ready(s_i2c_buses[i]));
  }

  // Preserve the shipping upgrade cleanup: old Obelix units can arrive here
  // with their superseded LIS2DW12 still running. PVT uses address 0x19; as in
  // shipping firmware, absence of the legacy part is harmless.
  (void)i2c_reg_write_byte(s_i2c_buses[1], 0x19, 0x21, 1U << 6U);

  // ponytail: shipping board_init() also calls mic_init() and audio_init().
  // pt2 has no PDM/audio-codec devicetree nodes or Zephyr drivers yet; add
  // those bindings/drivers, then invoke their Pebble adapters here.
  PBL_LOG_ALWAYS("FW_BOARD_DRIVERS_OK");
}

#endif  // CONFIG_PEBBLE_ZEPHYR_CORE_BOOT
