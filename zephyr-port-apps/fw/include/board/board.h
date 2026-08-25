/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

// Port shadow of src/fw/board/board.h. The shipping header chains to
// board_sf32lb52.h and the vendor HAL (GPIO/QSPI/UART typedefs) which this
// Zephyr-hosted build does not compile against. The only shipping app source
// that pulls board/board.h here (pbl/services/alarms/alarm.h) does not use any
// board symbol from it, so an empty shadow cuts the HAL cascade. The port's real
// board layer is the Zephyr devicetree/driver stack, not this header.
