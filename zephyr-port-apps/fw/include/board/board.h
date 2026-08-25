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
