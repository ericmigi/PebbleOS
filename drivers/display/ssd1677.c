/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "geometry.h"

#include <errno.h>
#include <string.h>

#include <pbl/devicetree.h>
#include <pbl/drivers/display/ssd1677.h>
#include <pbl/kernel/delay.h>
#include <pbl/kernel/irq.h>
#include <pbl/util/misc.h>

#define CMD_DRIVER_OUTPUT  0x01U
#define CMD_BOOSTER        0x0CU
#define CMD_DATA_ENTRY     0x11U
#define CMD_SW_RESET       0x12U
#define CMD_TEMP_SENSOR    0x18U
#define CMD_TEMP_WRITE     0x1AU
#define CMD_ACTIVATE       0x20U
#define CMD_UPDATE_CTRL1   0x21U
#define CMD_UPDATE_CTRL2   0x22U
#define CMD_WRITE_RAM_BW   0x24U
#define CMD_WRITE_RAM_RED  0x26U
#define CMD_BORDER         0x3CU
#define CMD_RAM_X_RANGE    0x44U
#define CMD_RAM_Y_RANGE    0x45U
#define CMD_AUTO_WRITE_BW  0x46U
#define CMD_AUTO_WRITE_RED 0x47U
#define CMD_RAM_X_COUNTER  0x4EU
#define CMD_RAM_Y_COUNTER  0x4FU

// Interlaced gate scan
#define DRIVER_OUTPUT_SCAN 0x02U
// X increments, Y decrements: the panel's gates run bottom to top
#define DATA_ENTRY_XINC_YDEC 0x01U
#define TEMP_SENSOR_INTERNAL 0x80U
// Fill the whole plane with white
#define AUTO_WRITE_WHITE 0xF7U
// Border waveforms, at init and for refreshes; most other projects use 0x01 for both
#define BORDER_INIT      0x80U
#define BORDER_REFRESH   0xC0U
#define CTRL1_NORMAL     0x00U
#define CTRL1_BYPASS_RED 0x40U
#define HALF_TEMP        0x5AU
#define SEQ_HALF         0xD7U
#define SEQ_FAST         0xFCU
#define SEQ_POWER_OFF    0x03U

#define RESET_MS 10U
// Reset, RAM fill and power off: 84 ms measured for the power on
#define BUSY_TIMEOUT_MS 1000U
// Measured: half 1.7 s, fast 0.5 s; longer in the cold
#define REFRESH_TIMEOUT_MS 5000U

enum refresh_mode {
  REFRESH_HALF,
  REFRESH_FAST,
};

static const uint8_t s_booster[] = {0xAEU, 0xC7U, 0xC3U, 0xC0U, 0x80U};

static const struct pbl_display_ssd1677 *prv_epd(const struct pbl_display *disp) {
  return PBL_CONTAINER_OF(disp, const struct pbl_display_ssd1677, disp);
}

static struct display_geometry prv_geometry(const struct pbl_display_ssd1677 *epd) {
  return (struct display_geometry){
    .width = epd->disp.props.width,
    .height = epd->disp.props.height,
    .panel_width = epd->panel_width,
    .panel_height = epd->panel_height,
    .scale = epd->scale,
    .quarter = epd->quarter,
  };
}

static int prv_cmd(const struct pbl_display_ssd1677 *epd, uint8_t cmd, const uint8_t *data,
                   size_t len) {
  int ret;

  (void)pbl_gpio_set(&epd->dc, false);
  ret = pbl_spi_write(&epd->spi, &cmd, 1U);
  (void)pbl_gpio_set(&epd->dc, true);
  if (ret != 0 || len == 0U) {
    return ret;
  }

  return pbl_spi_write(&epd->spi, data, len);
}

static int prv_cmd1(const struct pbl_display_ssd1677 *epd, uint8_t cmd, uint8_t data) {
  return prv_cmd(epd, cmd, &data, 1U);
}

static int prv_wait_busy(const struct pbl_display_ssd1677 *epd) {
  return PBL_POLL_SLEEP_MS(pbl_gpio_get(&epd->busy) == 0, 1U, BUSY_TIMEOUT_MS);
}

// The whole RAM, rows written top to bottom
static int prv_ram_window(const struct pbl_display_ssd1677 *epd) {
  struct display_geometry g = prv_geometry(epd);
  uint16_t x1 = display_geometry_scan_width(&g) - 1U;
  uint16_t y1 = display_geometry_scan_height(&g) - 1U;
  const uint8_t xr[] = {0U, 0U, x1 & 0xFFU, x1 >> 8};
  const uint8_t yr[] = {y1 & 0xFFU, y1 >> 8, 0U, 0U};
  const uint8_t xc[] = {0U, 0U};
  const uint8_t yc[] = {y1 & 0xFFU, y1 >> 8};
  int ret;

  ret = prv_cmd1(epd, CMD_DATA_ENTRY, DATA_ENTRY_XINC_YDEC);
  if (ret == 0) {
    ret = prv_cmd(epd, CMD_RAM_X_RANGE, xr, sizeof(xr));
  }
  if (ret == 0) {
    ret = prv_cmd(epd, CMD_RAM_Y_RANGE, yr, sizeof(yr));
  }
  if (ret == 0) {
    ret = prv_cmd(epd, CMD_RAM_X_COUNTER, xc, sizeof(xc));
  }
  if (ret == 0) {
    ret = prv_cmd(epd, CMD_RAM_Y_COUNTER, yc, sizeof(yc));
  }

  return ret;
}

// The shadow frame, scaled and turned, to a RAM plane, one controller row at a time
static int prv_write_plane(const struct pbl_display_ssd1677 *epd, uint8_t ram) {
  struct display_geometry g = prv_geometry(epd);
  bool flip = epd->state->flip;
  uint32_t pitch = (g.width + 7U) / 8U;
  uint32_t row_len = display_geometry_scan_width(&g) / 8U;
  uint32_t rows = display_geometry_scan_height(&g);
  int ret;

  ret = prv_ram_window(epd);
  if (ret == 0) {
    ret = prv_cmd(epd, ram, nullptr, 0U);
  }

  for (uint32_t cy = 0U; ret == 0 && cy < rows; cy++) {
    int32_t across = display_geometry_across(&g, flip, cy);

    if (across < 0) {
      memset(epd->row, 0xFF, row_len);
    } else {
      for (uint32_t i = 0U; i < row_len; i++) {
        uint8_t byte = 0U;

        // RAM bytes are most significant bit first, the framebuffer's least; 1 is white
        for (uint32_t bit = 0U; bit < 8U; bit++) {
          int32_t along = epd->along[i * 8U + bit];
          uint32_t white = 1U;

          if (along >= 0) {
            uint32_t x = (uint32_t)(g.quarter ? across : along);
            uint32_t y = (uint32_t)(g.quarter ? along : across);

            white = (epd->shadow[y * pitch + x / 8U] >> (x % 8U)) & 1U;
          }
          byte |= (uint8_t)(white << (7U - bit));
        }
        epd->row[i] = byte;
      }
    }

    ret = pbl_spi_write(&epd->spi, epd->row, row_len);
  }

  return ret;
}

// Ends the refresh in flight once, from the BUSY interrupt or the timeout
static void prv_end(const struct pbl_display *disp, int status) {
  struct pbl_display_ssd1677_state *state = prv_epd(disp)->state;
  bool refreshing;

  pbl_irq_lock();
  refreshing = state->refreshing;
  state->refreshing = false;
  pbl_irq_unlock();

  if (!refreshing) {
    return;
  }

  (void)pbl_intc_disable(&state->busy_irq);
  pbl_timer_stop(&state->timer);
  if (status != 0) {
    state->need_half = true;
  }

  pbl_display_complete(disp, status);
}

static void prv_busy_isr(void *arg) {
  prv_end(arg, 0);
}

static void prv_timeout(struct pbl_timer *timer) {
  struct pbl_display_ssd1677_state *state =
      PBL_CONTAINER_OF(timer, struct pbl_display_ssd1677_state, timer);

  prv_end(state->disp, -ETIMEDOUT);
}

static int prv_controller_init(const struct pbl_display_ssd1677 *epd) {
  struct display_geometry g = prv_geometry(epd);
  uint16_t gates = display_geometry_scan_height(&g) - 1U;
  const uint8_t output[] = {gates & 0xFFU, gates >> 8, DRIVER_OUTPUT_SCAN};
  int ret;

  // High, low and high again, 10 ms each, as the stock firmware does
  pbl_sleep_ms(RESET_MS);
  (void)pbl_gpio_set(&epd->reset, true);
  pbl_sleep_ms(RESET_MS);
  (void)pbl_gpio_set(&epd->reset, false);
  pbl_sleep_ms(RESET_MS);

  ret = prv_cmd(epd, CMD_SW_RESET, nullptr, 0U);
  if (ret != 0) {
    return ret;
  }
  // BUSY may not be up yet at once
  pbl_sleep_ms(RESET_MS);
  ret = prv_wait_busy(epd);

  if (ret == 0) {
    ret = prv_cmd1(epd, CMD_TEMP_SENSOR, TEMP_SENSOR_INTERNAL);
  }
  if (ret == 0) {
    ret = prv_cmd(epd, CMD_BOOSTER, s_booster, sizeof(s_booster));
  }
  if (ret == 0) {
    ret = prv_cmd(epd, CMD_DRIVER_OUTPUT, output, sizeof(output));
  }
  if (ret == 0) {
    ret = prv_cmd1(epd, CMD_BORDER, BORDER_INIT);
  }
  if (ret == 0) {
    ret = prv_ram_window(epd);
  }
  if (ret == 0) {
    ret = prv_cmd1(epd, CMD_AUTO_WRITE_BW, AUTO_WRITE_WHITE);
  }
  if (ret == 0) {
    ret = prv_wait_busy(epd);
  }
  if (ret == 0) {
    ret = prv_cmd1(epd, CMD_AUTO_WRITE_RED, AUTO_WRITE_WHITE);
  }
  if (ret == 0) {
    ret = prv_wait_busy(epd);
  }

  return ret;
}

static int prv_init(const struct pbl_display *disp) {
  const struct pbl_display_ssd1677 *epd = prv_epd(disp);
  struct pbl_display_ssd1677_state *state = epd->state;
  uint32_t edge;
  int ret;

  if (pbl_device_init(&epd->spi.bus->dev) != 0 || pbl_device_init(&epd->dc.port->dev) != 0 ||
      pbl_device_init(&epd->reset.port->dev) != 0 || pbl_device_init(&epd->busy.port->dev) != 0) {
    return -ENODEV;
  }

  ret = pbl_spi_setup(&epd->spi);
  if (ret == 0) {
    ret = pbl_gpio_configure(&epd->dc, PBL_GPIO_OUTPUT_ACTIVE);
  }
  if (ret == 0) {
    ret = pbl_gpio_configure(&epd->reset, PBL_GPIO_OUTPUT_INACTIVE);
  }
  if (ret == 0) {
    ret = pbl_gpio_configure(&epd->busy, PBL_GPIO_INPUT);
  }
  if (ret != 0) {
    return ret;
  }

  // BUSY drops when a refresh ends
  edge = ((epd->busy.flags & PBL_GPIO_ACTIVE_LOW) != 0U) ? PBL_IRQ_TYPE_EDGE_RISING
                                                         : PBL_IRQ_TYPE_EDGE_FALLING;
  ret = pbl_gpio_to_irq(&epd->busy, edge, &state->busy_irq);
  if (ret != 0) {
    return ret;
  }
  if (pbl_device_init(&state->busy_irq.intc->dev) != 0) {
    return -ENODEV;
  }

  state->disp = disp;
  state->refreshing = false;
  state->need_half = true;
  state->updates = 0U;
  pbl_timer_init(&state->timer, prv_timeout);

  ret = pbl_intc_request(&state->busy_irq, prv_busy_isr, (void *)disp);
  if (ret != 0) {
    return ret;
  }

  // Unknown until the first update: the frame is white, as the RAM
  memset(epd->shadow, 0xFF, (disp->props.width + 7U) / 8U * disp->props.height);

  return prv_controller_init(epd);
}

static int prv_enable(const struct pbl_display *disp) {
  return 0;
}

static int prv_disable(const struct pbl_display *disp) {
  const struct pbl_display_ssd1677 *epd = prv_epd(disp);
  int ret;

  if (epd->state->refreshing) {
    return -EBUSY;
  }

  ret = prv_cmd1(epd, CMD_BORDER, BORDER_INIT);
  if (ret == 0) {
    ret = prv_cmd1(epd, CMD_UPDATE_CTRL2, SEQ_POWER_OFF);
  }
  if (ret == 0) {
    ret = prv_cmd(epd, CMD_ACTIVATE, nullptr, 0U);
  }
  if (ret == 0) {
    ret = prv_wait_busy(epd);
  }

  epd->state->need_half = true;

  return ret;
}

static int prv_set_rotated(const struct pbl_display *disp, bool rotated) {
  const struct pbl_display_ssd1677 *epd = prv_epd(disp);
  struct display_geometry g = prv_geometry(epd);

  epd->state->flip = rotated;
  for (uint32_t cx = 0U; cx < display_geometry_scan_width(&g); cx++) {
    epd->along[cx] = (int16_t)display_geometry_along(&g, rotated, cx);
  }

  return 0;
}

static enum refresh_mode prv_mode(const struct pbl_display_ssd1677 *epd) {
  struct pbl_display_ssd1677_state *state = epd->state;

  if (state->need_half) {
    state->need_half = false;
    state->updates = 0U;
    return REFRESH_HALF;
  }

  // The periodic clean refresh is the half one too, the stock firmware's only one: the OTP full
  // waveform (22=F7) takes 3.7 s here
  if (epd->full_interval != 0U && ++state->updates >= epd->full_interval) {
    state->updates = 0U;
    return REFRESH_HALF;
  }

  return REFRESH_FAST;
}

static int prv_refresh(const struct pbl_display *disp, enum refresh_mode mode) {
  const struct pbl_display_ssd1677 *epd = prv_epd(disp);
  struct pbl_display_ssd1677_state *state = epd->state;
  uint8_t ctrl1 = (mode == REFRESH_FAST) ? CTRL1_NORMAL : CTRL1_BYPASS_RED;
  uint8_t seq = (mode == REFRESH_HALF) ? SEQ_HALF : SEQ_FAST;
  int ret;

  ret = prv_cmd1(epd, CMD_UPDATE_CTRL1, ctrl1);
  if (ret == 0) {
    ret = prv_cmd1(epd, CMD_BORDER, BORDER_REFRESH);
  }
  if (ret == 0 && mode == REFRESH_HALF) {
    ret = prv_cmd1(epd, CMD_TEMP_WRITE, HALF_TEMP);
  }
  if (ret == 0) {
    ret = prv_cmd1(epd, CMD_UPDATE_CTRL2, seq);
  }
  if (ret != 0) {
    return ret;
  }

  // The interrupt is armed before the refresh starts, so its end cannot be missed; the deadline
  // runs from the activation, however late that comes
  state->refreshing = true;
  ret = pbl_intc_enable(&state->busy_irq);
  if (ret == 0) {
    ret = prv_cmd(epd, CMD_ACTIVATE, nullptr, 0U);
  }

  bool refreshing;

  pbl_irq_lock();
  if (ret == 0) {
    // Not once the interrupt ended it: a stale deadline would end the next refresh
    if (state->refreshing) {
      pbl_timer_start(&state->timer, PBL_MSEC(REFRESH_TIMEOUT_MS));
    }
    pbl_irq_unlock();
    return 0;
  }
  refreshing = state->refreshing;
  state->refreshing = false;
  pbl_irq_unlock();
  (void)pbl_intc_disable(&state->busy_irq);

  // A refresh the activation started anyway has ended through the interrupt, which completed it
  return refreshing ? ret : 0;
}

static int prv_update(const struct pbl_display *disp, const struct pbl_display_buffer *buf,
                      uint16_t y0, uint16_t y1) {
  const struct pbl_display_ssd1677 *epd = prv_epd(disp);
  uint32_t pitch = (disp->props.width + 7U) / 8U;
  enum refresh_mode mode;
  int ret = 0;

  // Still busy with a refresh that timed out: it would ignore us, then end this update early
  if (pbl_gpio_get(&epd->busy) != 0) {
    return -EBUSY;
  }

  mode = prv_mode(epd);

  // The old image, which a fast refresh compares the new one with
  if (mode == REFRESH_FAST) {
    ret = prv_write_plane(epd, CMD_WRITE_RAM_RED);
  }

  for (uint32_t y = y0; y <= y1; y++) {
    memcpy(&epd->shadow[y * pitch], &buf->data[y * buf->pitch], pitch);
  }

  if (ret == 0) {
    ret = prv_write_plane(epd, CMD_WRITE_RAM_BW);
  }
  // The half refresh is not blind to the old plane, whatever 0x21 says: without the new image
  // there too, what it turns black greys out at the next fast refresh
  if (ret == 0 && mode == REFRESH_HALF) {
    ret = prv_write_plane(epd, CMD_WRITE_RAM_RED);
  }
  if (ret == 0) {
    ret = prv_refresh(disp, mode);
  }
  if (ret != 0) {
    epd->state->need_half = true;
  }

  return ret;
}

const struct pbl_display_ops pbl_display_ssd1677_ops = {
  .init = prv_init,
  .enable = prv_enable,
  .disable = prv_disable,
  .set_rotated = prv_set_rotated,
  .update = prv_update,
};

#include PBL_DT_INSTANCES(solomon_ssd1677)
