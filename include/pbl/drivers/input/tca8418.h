/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <pbl/device.h>
#include <pbl/drivers/gpio.h>
#include <pbl/drivers/i2c.h>
#include <pbl/drivers/intc.h>
#include <pbl/kernel/mutex.h>
#include <pbl/kernel/work.h>
#include <pbl/util/size.h>

/**
 * @defgroup drivers_input_tca8418 TCA8418
 * @ingroup drivers
 * @brief TI TCA8418 keypad scanner, reported as @ref input key events.
 *
 * The chip scans a matrix of up to 8 rows and 10 columns and queues each press and release in a
 * FIFO, pulling its open-drain interrupt line low until the FIFO is read. The events are read on
 * the system work queue, each reported on its own with the code the keymap gives the key; keys
 * the keymap leaves out are dropped.
 *
 * The line is edge triggered, so a line still held low after the FIFO is drained, such as when
 * the chip queued an event between the last read and the acknowledgement, is serviced again
 * every 50 ms until it is released. The chip is programmed at init, with the FIFO flushed, and
 * again by pbl_tca8418_set_enabled() for a board that cuts its supply.
 *
 * The configuration follows the devicetree ti,tca8418 binding, with the keymap of the
 * matrix-keymap binding:
 *
 * @code{.c}
 * PBL_TCA8418_DEFINE(keypad, "keypad", &i2c0, 0x34,
 *                    PBL_IRQ_SPEC(&gpio1_intc, 14, PBL_IRQ_TYPE_EDGE_FALLING),
 *                    PBL_GPIO(&gpio1, 14, 0), 7, 7,
 *                    MATRIX_KEY(0, 1, KEY_W), MATRIX_KEY(0, 2, KEY_G), ...);
 * @endcode
 * @{
 */

/** @cond INTERNAL_HIDDEN */
struct pbl_tca8418;

struct pbl_tca8418_state {
  const struct pbl_tca8418 *tca;
  struct pbl_mutex lock;
  /** Reads the events after an interrupt, boosted past the apps. */
  struct pbl_work irq_work;
  /** Services the chip again while its interrupt line stays asserted. */
  struct pbl_delayed_work work;
  bool enabled;
};
/** @endcond */

/** @brief TCA8418. */
struct pbl_tca8418 {
  /** Device, child of the I2C bus. */
  struct pbl_device dev;
  /** Driver state. */
  struct pbl_tca8418_state *state;
  /** I2C device. */
  struct pbl_i2c_dev i2c;
  /** Interrupt, pulled low while events are queued, @c interrupts. */
  struct pbl_irq_spec irq;
  /** Level of the interrupt line, read back to service it while it stays low. */
  struct pbl_gpio int_in;
  /** Keymap, each entry from MATRIX_KEY(), @c linux,keymap. */
  const uint32_t *keymap;
  /** Number of keymap entries. */
  uint8_t keymap_len;
  /** Rows of the matrix, @c keypad,num-rows. */
  uint8_t rows;
  /** Columns of the matrix, @c keypad,num-columns. */
  uint8_t cols;
};

/** @cond INTERNAL_HIDDEN */
int pbl_tca8418_init(const struct pbl_device *dev);
/** @endcond */

/**
 * @brief Stop or restart reporting keys.
 *
 * Stopping masks the chip's interrupt and stops servicing it. Restarting programs the chip again,
 * retrying a few times, so a board calls it after the chip's supply comes back. Thread context;
 * not from an input listener.
 *
 * @param dev TCA8418 device.
 * @param enabled Whether to report keys.
 * @return 0, @c -ENODEV if the device is not ready, or a negative errno from the chip.
 */
int pbl_tca8418_set_enabled(const struct pbl_device *dev, bool enabled);

/**
 * @brief Define a TCA8418.
 *
 * @param sym Symbol of the device.
 * @param _name Device name.
 * @param _bus I2C bus.
 * @param _addr 7-bit I2C address.
 * @param _irq Interrupt, from PBL_IRQ_SPEC().
 * @param _int_in Interrupt line read back as a GPIO, from PBL_GPIO(), with no flags.
 * @param _rows Rows of the matrix.
 * @param _cols Columns of the matrix.
 * @param ... Keymap entries, each from MATRIX_KEY(row, column, code).
 */
#define PBL_TCA8418_DEFINE(sym, _name, _bus, _addr, _irq, _int_in, _rows, _cols, ...)      \
  static const uint32_t sym##_keymap[] = {__VA_ARGS__};                                    \
  static struct pbl_tca8418_state sym##_tca8418_state;                                     \
  PBL_DEVICE_STATE_DEFINE(sym);                                                            \
  PBL_DEVICE_INSTANCE_DEV(sym, struct pbl_tca8418) {                                       \
    .dev = PBL_DEVICE_INIT(sym, _name, pbl_tca8418_init, &(_bus)->dev),                    \
    .state = &sym##_tca8418_state, .i2c = PBL_I2C_DEV(_bus, _addr), .irq = _irq,           \
    .int_in = _int_in, .keymap = sym##_keymap, .keymap_len = PBL_ARRAY_SIZE(sym##_keymap), \
    .rows = (_rows), .cols = (_cols),                                                      \
  }

/** @} */
