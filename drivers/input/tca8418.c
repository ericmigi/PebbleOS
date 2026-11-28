/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>

#include <pbl/devicetree.h>
#include <pbl/drivers/input/tca8418.h>
#include <pbl/input/input.h>
#include <pbl/kernel/delay.h>
#include <pbl/logging/logging.h>
#include <pbl/util/bits.h>
#include <pbl/util/misc.h>

PBL_LOG_MODULE_DEFINE(driver_input_tca8418, LOG_LEVEL_INFO);

#define TCA8418_REG_CFG            0x01U
#define TCA8418_REG_INT_STAT       0x02U
#define TCA8418_REG_KEY_LCK_EC     0x03U
#define TCA8418_REG_KEY_EVENT_A    0x04U
#define TCA8418_REG_GPIO_INT_STAT1 0x11U
#define TCA8418_REG_GPIO_INT_EN1   0x1AU
#define TCA8418_REG_KP_GPIO1       0x1DU
#define TCA8418_REG_GPI_EM1        0x20U
#define TCA8418_REG_GPIO_DIR1      0x23U
#define TCA8418_REG_GPIO_INT_LVL1  0x26U

#define TCA8418_CFG_KE_IEN       PBL_BIT(0)
#define TCA8418_CFG_GPI_IEN      PBL_BIT(1)
#define TCA8418_CFG_OVR_FLOW_IEN PBL_BIT(3)
#define TCA8418_CFG_INT_CFG      PBL_BIT(4)
#define TCA8418_CFG_IEN_MASK (TCA8418_CFG_KE_IEN | TCA8418_CFG_GPI_IEN | TCA8418_CFG_OVR_FLOW_IEN)

#define TCA8418_INT_STAT_K_INT        PBL_BIT(0)
#define TCA8418_INT_STAT_GPI_INT      PBL_BIT(1)
#define TCA8418_INT_STAT_OVR_FLOW_INT PBL_BIT(3)

#define TCA8418_KEY_LCK_EC_KEC_MASK PBL_GENMASK(3, 0)
#define TCA8418_EVENT_PRESSED       PBL_BIT(7)
#define TCA8418_EVENT_KEY_MASK      PBL_GENMASK(6, 0)

#define TCA8418_MAX_ROWS 8U
#define TCA8418_MAX_COLS 10U
// Key numbers count from 1, ten per row
#define TCA8418_KEYS_PER_ROW 10U

// Time for the scan to settle at power-up before the FIFO is flushed
#define TCA8418_SETTLE_MS     50U
#define TCA8418_PROGRAM_TRIES 3U
#define TCA8418_RETRY_MS      10U
// Period at which a line still held low is serviced again
#define TCA8418_RECOVERY_MS 50U
// Bound on the events read in one pass; the FIFO holds 10
#define TCA8418_MAX_EVENTS 32U

static int prv_read(const struct pbl_tca8418 *tca, uint8_t reg, uint8_t *val) {
  int ret = pbl_i2c_reg_read_byte(&tca->i2c, reg, val);

  if (ret != 0) {
    PBL_LOG_ERR("Read of 0x%02x failed (%d)", reg, ret);
  }

  return ret;
}

static int prv_write(const struct pbl_tca8418 *tca, uint8_t reg, uint8_t val) {
  int ret = pbl_i2c_reg_write_byte(&tca->i2c, reg, val);

  if (ret != 0) {
    PBL_LOG_ERR("Write of 0x%02x to 0x%02x failed (%d)", val, reg, ret);
  }

  return ret;
}

static bool prv_int_asserted(const struct pbl_tca8418 *tca) {
  return pbl_gpio_get(&tca->int_in) == 0;
}

// Empties the FIFO and acknowledges what it raised, dropping the events
static int prv_flush(const struct pbl_tca8418 *tca) {
  uint8_t val;
  int ret;

  for (uint32_t i = 0U; i < TCA8418_MAX_EVENTS; i++) {
    ret = prv_read(tca, TCA8418_REG_KEY_EVENT_A, &val);
    if (ret != 0) {
      return ret;
    }
    if (val == 0U) {
      break;
    }
    PBL_LOG_DBG("Dropped event 0x%02x", val);
  }

  for (uint8_t i = 0U; i < 3U; i++) {
    ret = prv_read(tca, TCA8418_REG_GPIO_INT_STAT1 + i, &val);
    if (ret != 0) {
      return ret;
    }
  }

  return prv_write(
      tca, TCA8418_REG_INT_STAT,
      TCA8418_INT_STAT_K_INT | TCA8418_INT_STAT_GPI_INT | TCA8418_INT_STAT_OVR_FLOW_INT);
}

static int prv_program(const struct pbl_tca8418 *tca) {
  const uint32_t rows = PBL_GENMASK(tca->rows - 1U, 0U);
  const uint32_t cols = PBL_GENMASK(tca->cols - 1U, 0U);
  const uint8_t regs[][2] = {
    {TCA8418_REG_CFG, 0U},
    {TCA8418_REG_KP_GPIO1, (uint8_t)rows},
    {TCA8418_REG_KP_GPIO1 + 1U, (uint8_t)cols},
    {TCA8418_REG_KP_GPIO1 + 2U, (uint8_t)(cols >> 8)},
    // The pins left out of the matrix are quiet inputs
    {TCA8418_REG_GPIO_DIR1, 0U},
    {TCA8418_REG_GPIO_DIR1 + 1U, 0U},
    {TCA8418_REG_GPIO_DIR1 + 2U, 0U},
    {TCA8418_REG_GPIO_INT_EN1, 0U},
    {TCA8418_REG_GPIO_INT_EN1 + 1U, 0U},
    {TCA8418_REG_GPIO_INT_EN1 + 2U, 0U},
    {TCA8418_REG_GPI_EM1, 0U},
    {TCA8418_REG_GPI_EM1 + 1U, 0U},
    {TCA8418_REG_GPI_EM1 + 2U, 0U},
    {TCA8418_REG_GPIO_INT_LVL1, 0U},
    {TCA8418_REG_GPIO_INT_LVL1 + 1U, 0U},
    {TCA8418_REG_GPIO_INT_LVL1 + 2U, 0U},
  };
  int ret;

  for (size_t i = 0U; i < PBL_ARRAY_SIZE(regs); i++) {
    ret = prv_write(tca, regs[i][0], regs[i][1]);
    if (ret != 0) {
      return ret;
    }
  }

  // Keys held while the chip came up are not reported
  pbl_sleep_ms(TCA8418_SETTLE_MS);

  ret = prv_flush(tca);
  if (ret != 0) {
    return ret;
  }

  // The line pulses when events are still queued as it is acknowledged, raising a new edge
  return prv_write(tca, TCA8418_REG_CFG,
                   TCA8418_CFG_INT_CFG | TCA8418_CFG_OVR_FLOW_IEN | TCA8418_CFG_KE_IEN);
}

static void prv_report(const struct pbl_tca8418 *tca, uint8_t event) {
  const uint8_t key = event & TCA8418_EVENT_KEY_MASK;
  const bool pressed = (event & TCA8418_EVENT_PRESSED) != 0U;
  uint8_t row;
  uint8_t col;

  if (key == 0U || key > TCA8418_MAX_ROWS * TCA8418_KEYS_PER_ROW) {
    PBL_LOG_DBG("Ignored event 0x%02x", event);
    return;
  }

  row = (key - 1U) / TCA8418_KEYS_PER_ROW;
  col = (key - 1U) % TCA8418_KEYS_PER_ROW;

  for (uint8_t i = 0U; i < tca->keymap_len; i++) {
    const uint32_t entry = tca->keymap[i];

    if (PBL_FIELD_GET(PBL_GENMASK(31, 24), entry) == row &&
        PBL_FIELD_GET(PBL_GENMASK(23, 16), entry) == col) {
      pbl_input_report_key((uint16_t)PBL_FIELD_GET(PBL_GENMASK(15, 0), entry), pressed, true);
      return;
    }
  }

  PBL_LOG_DBG("Unmapped key at row %u column %u", row, col);
}

static void prv_service(struct pbl_tca8418_state *state) {
  const struct pbl_tca8418 *tca = state->tca;
  uint32_t processed = 0U;
  uint8_t int_stat;
  uint8_t count;
  uint8_t event;

  pbl_mutex_lock(&state->lock, PBL_FOREVER);

  if (!state->enabled) {
    pbl_mutex_unlock(&state->lock);
    return;
  }

  if (prv_read(tca, TCA8418_REG_INT_STAT, &int_stat) != 0 ||
      prv_read(tca, TCA8418_REG_KEY_LCK_EC, &count) != 0) {
    goto out;
  }
  count = PBL_FIELD_GET(TCA8418_KEY_LCK_EC_KEC_MASK, count);

  while (count > 0U && processed < TCA8418_MAX_EVENTS) {
    if (prv_read(tca, TCA8418_REG_KEY_EVENT_A, &event) != 0) {
      goto out;
    }
    if (event == 0U) {
      break;
    }

    prv_report(tca, event);
    processed++;

    if (prv_read(tca, TCA8418_REG_KEY_LCK_EC, &count) != 0) {
      goto out;
    }
    count = PBL_FIELD_GET(TCA8418_KEY_LCK_EC_KEC_MASK, count);
  }

  if ((int_stat & TCA8418_INT_STAT_K_INT) != 0U || processed > 0U) {
    (void)prv_write(tca, TCA8418_REG_INT_STAT, TCA8418_INT_STAT_K_INT);
  }

  if ((int_stat & TCA8418_INT_STAT_OVR_FLOW_INT) != 0U) {
    PBL_LOG_WRN("FIFO overflow");
    (void)prv_flush(tca);
  }

out:
  // Edges are lost while the line stays low, so it is serviced until it is released
  if (prv_int_asserted(tca)) {
    (void)pbl_delayed_work_schedule(&pbl_sys_workq, &state->work, PBL_MSEC(TCA8418_RECOVERY_MS));
  }

  pbl_mutex_unlock(&state->lock);
}

static void prv_irq_work(struct pbl_work *work) {
  prv_service(PBL_CONTAINER_OF(work, struct pbl_tca8418_state, irq_work));
}

static void prv_retry_work(struct pbl_work *work) {
  prv_service(PBL_CONTAINER_OF(work, struct pbl_tca8418_state, work.work));
}

// Keys must not wait for an app hogging the CPU, hence the boost
static void prv_kick(struct pbl_tca8418_state *state) {
  (void)pbl_work_submit_boosted(&pbl_sys_workq, &state->irq_work);
}

static void prv_irq_handler(void *arg) {
  prv_kick(arg);
}

// Called with the lock held
static int prv_start(const struct pbl_tca8418 *tca) {
  struct pbl_tca8418_state *state = tca->state;
  int ret = -EIO;

  for (uint32_t i = 0U; i < TCA8418_PROGRAM_TRIES; i++) {
    if (i > 0U) {
      pbl_sleep_ms(TCA8418_RETRY_MS);
    }
    ret = prv_program(tca);
    if (ret == 0) {
      break;
    }
  }
  if (ret != 0) {
    return ret;
  }

  ret = pbl_intc_enable(&tca->irq);
  if (ret != 0) {
    return ret;
  }

  state->enabled = true;

  // An event queued since the flush has already pulled the line low
  if (prv_int_asserted(tca)) {
    prv_kick(state);
  }

  return 0;
}

int pbl_tca8418_set_enabled(const struct pbl_device *dev, bool enabled) {
  const struct pbl_tca8418 *tca = PBL_CONTAINER_OF(dev, const struct pbl_tca8418, dev);
  struct pbl_tca8418_state *state = tca->state;
  uint8_t cfg;
  int ret = 0;

  if (!pbl_device_is_ready(dev)) {
    return -ENODEV;
  }

  pbl_mutex_lock(&state->lock, PBL_FOREVER);

  if (enabled) {
    if (!state->enabled) {
      ret = prv_start(tca);
    }
    pbl_mutex_unlock(&state->lock);
    return ret;
  }

  if (state->enabled) {
    state->enabled = false;
    (void)pbl_intc_disable(&tca->irq);
    // The chip may be unpowered already
    if (pbl_i2c_reg_read_byte(&tca->i2c, TCA8418_REG_CFG, &cfg) == 0) {
      (void)pbl_i2c_reg_write_byte(&tca->i2c, TCA8418_REG_CFG, cfg & ~TCA8418_CFG_IEN_MASK);
    }
  }

  pbl_mutex_unlock(&state->lock);

  (void)pbl_work_cancel_sync(&state->irq_work);
  (void)pbl_delayed_work_cancel_sync(&state->work);

  return 0;
}

int pbl_tca8418_init(const struct pbl_device *dev) {
  const struct pbl_tca8418 *tca = PBL_CONTAINER_OF(dev, const struct pbl_tca8418, dev);
  struct pbl_tca8418_state *state = tca->state;
  uint8_t cfg;
  int ret;

  if (tca->rows == 0U || tca->rows > TCA8418_MAX_ROWS || tca->cols == 0U ||
      tca->cols > TCA8418_MAX_COLS) {
    return -EINVAL;
  }

  state->tca = tca;
  state->enabled = false;
  pbl_mutex_init(&state->lock);
  pbl_work_init(&state->irq_work, prv_irq_work);
  pbl_delayed_work_init(&state->work, prv_retry_work);

  if (pbl_i2c_reg_read_byte(&tca->i2c, TCA8418_REG_CFG, &cfg) != 0) {
    return -ENODEV;
  }

  ret = pbl_device_init(&tca->int_in.port->dev);
  if (ret != 0) {
    return ret;
  }

  // The chip drives the line open-drain
  ret = pbl_gpio_configure(&tca->int_in, PBL_GPIO_INPUT | PBL_GPIO_PULL_UP);
  if (ret != 0) {
    return ret;
  }

  ret = pbl_device_init(&tca->irq.intc->dev);
  if (ret != 0) {
    return ret;
  }

  ret = pbl_intc_request(&tca->irq, prv_irq_handler, state);
  if (ret != 0) {
    return ret;
  }

  pbl_mutex_lock(&state->lock, PBL_FOREVER);
  ret = prv_start(tca);
  pbl_mutex_unlock(&state->lock);

  return ret;
}

#include PBL_DT_INSTANCES(ti_tca8418)
