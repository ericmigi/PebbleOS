/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

// Port shadow of src/fw/board/board.h. The shipping header chains to
// board_sf32lb52.h and the vendor HAL (GPIO/QSPI/UART typedefs) which this
// Zephyr-hosted build does not compile against. The only shipping app source
// that pulls board/board.h here (pbl/services/alarms/alarm.h) does not use any
// board symbol from it, so an empty shadow cuts the HAL cascade. The port's real
// board layer is the Zephyr devicetree/driver stack, not this header.

// The shipping QSPI flash definitions (pbl/drivers/flash/qspi_flash_definitions.h)
// reference OutputConfig for an (unused on obelix) flash reset GPIO. Provide the
// minimal typedef here so those headers compile without the full board chain.
#include <stdbool.h>
#include <stdint.h>

#ifndef PORT_BOARD_OUTPUTCONFIG_DEFINED
#define PORT_BOARD_OUTPUTCONFIG_DEFINED
typedef struct {
  void *gpio;
  uint8_t gpio_pin;
  bool active_high;
} OutputConfig;
#endif

// shell_prefs_init reads the board backlight/accel defaults; minimal shapes
// matching board_qemu.h + instances in settings_system_glue.c.
typedef struct {
  uint8_t backlight_on_percent;
  uint32_t ambient_light_dark_threshold;
  uint32_t ambient_k_delta_threshold;
  uint32_t ambient_light_lux_dark_offset;
  uint32_t ambient_light_lux_num;
  uint32_t ambient_light_lux_den;
  uint32_t backlight_default_color;
} BoardConfig;

typedef struct {
  uint8_t default_motion_sensitivity;
} BoardConfigAccel;

extern const BoardConfig BOARD_CONFIG;
extern const BoardConfigAccel BOARD_CONFIG_ACCEL;
