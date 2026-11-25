/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/drivers/flash.h>
#include <pbl/util/units.h>

/**
 * @defgroup drivers_flash_esp32s3 ESP32-S3
 * @ingroup drivers_flash
 * @brief @ref drivers_flash driver for the flash an ESP32-S3 runs from, through the ROM's SPI1
 * routines.
 *
 * Every operation runs with interrupts masked and both ICache and DCache suspended, from IRAM,
 * as the CPU executes from the same flash. Reads inside the mapped XIP window are plain memory
 * reads.
 * @{
 */

/** @brief ESP32-S3 flash. */
struct pbl_flash_esp32s3 {
  /** Flash. */
  struct pbl_flash flash;
};

/** @cond INTERNAL_HIDDEN */
extern const struct pbl_flash_ops pbl_flash_esp32s3_ops;
/** @endcond */

/**
 * @brief Define the ESP32-S3 flash.
 *
 * @param sym Symbol of the flash.
 * @param _name Name.
 * @param _size Size, in bytes.
 */
#define PBL_FLASH_ESP32S3_DEFINE(sym, _name, _size)                                                \
  PBL_FLASH_STATE_DEFINE(sym);                                                                     \
  static const struct pbl_flash_geometry sym##_geometry = {                                        \
    .size = (_size),                                                                               \
    .page_size = 256U,                                                                             \
    .sector_size = PBL_KIB(64),                                                                    \
    .subsector_size = PBL_KIB(4),                                                                  \
    .sector_erase_ms = 150U,                                                                       \
    .subsector_erase_ms = 45U,                                                                     \
  };                                                                                               \
  PBL_DEVICE_INSTANCE(sym, struct pbl_flash_esp32s3, struct pbl_flash, flash) {                    \
    .flash = PBL_FLASH_INIT(sym, _name, nullptr, &pbl_flash_esp32s3_ops, &sym##_geometry, nullptr, \
                            nullptr),                                                              \
  }

/** @} */
