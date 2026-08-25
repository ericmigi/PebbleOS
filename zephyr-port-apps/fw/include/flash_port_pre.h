/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

// Force-included (via CMake -include) ahead of the shipping QSPI flash sources
// (src/fw/drivers/flash/gd25q256e.c, src/fw/drivers/sf32lb52/qspi.c) and the
// port's qspi_board.c, to reproduce the include environment those files get in
// the shipping build.

// 1. Zephyr's sys/util.h and Pebble's pbl/util/math.h both declare sign_extend()
//    with different signatures. Pull Zephyr in with it renamed so the Pebble
//    declaration wins (mirrors fw_zephyr_pre.h).
#define sign_extend zephyr_sign_extend
#include <zephyr/kernel.h>
#undef sign_extend

// 2. Include the SiFli HAL umbrella FIRST. register.h re-enters bf0_hal.h, which
//    pulls the per-module headers (adc/i2c/mpi/...) that reference
//    DMA_HandleTypeDef. If bf0_hal_dma.h is included first instead (as happens
//    via pbl/drivers/qspi_definitions.h), that cascade runs before
//    DMA_HandleTypeDef is defined. Including bf0_hal.h up front makes its include
//    guard absorb the re-entry so every module header resolves in order.
#include "bf0_hal.h"

// 3. The shipping flash driver references the board's QSPI objects, declared in
//    board_obelix.h in shipping. The port uses an empty board.h shadow, so
//    declare them here. Definitions live in qspi_board.c.
#include <pbl/drivers/flash/qspi_flash.h>
extern QSPIPort *const QSPI;
extern QSPIFlash *const QSPI_FLASH;

// 4. FreeRTOS-style critical sections used by qspi.c, mapped onto Zephyr's IRQ
//    lock.
#include "port_crit.h"
