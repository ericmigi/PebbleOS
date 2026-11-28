/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <string.h>

#include <pbl/devicetree.h>
#include <pbl/drivers/display.h>
#include <pbl/drivers/gpio.h>
#include <pbl/drivers/spi.h>
#include <pbl/kernel/delay.h>
#include <pbl/kernel/work.h>
#include <pbl/logging/logging.h>
#include <pbl/util/misc.h>

PBL_LOG_MODULE_DEFINE(uc8253, LOG_LEVEL_DEBUG);

enum {
  PSR = 0x00,
  POWER_OFF = 0x02,
  POWER_ON = 0x04,
  OLD_IMAGE = 0x10,
  REFRESH = 0x12,
  NEW_IMAGE = 0x13,
  CDI = 0x50,
  RESOLUTION = 0x61,
  PARTIAL_WINDOW = 0x90,
  PARTIAL_IN = 0x91,
  PARTIAL_OUT = 0x92,
  CCSET = 0xe0,
  TSSET = 0xe5,
  POLL_MS = 20,
  TIMEOUT_MS = 5000,
};

struct uc8253_state {
  struct pbl_delayed_work poll;
  const struct pbl_display *disp;
  bool valid;
  bool partial;
  bool rotated;
  unsigned elapsed_ms;
};

struct uc8253 {
  struct pbl_display disp;
  struct pbl_spi_dev spi;
  struct pbl_gpio dc, busy, sd_select, radio_select;
  struct uc8253_state *state;
  uint8_t *shown, *next;
};

static const struct uc8253 *prv_panel(const struct pbl_display *disp) {
  return PBL_CONTAINER_OF(disp, const struct uc8253, disp);
}

static int prv_send(const struct uc8253 *panel, uint8_t cmd, const uint8_t *data, size_t len) {
  int ret = pbl_gpio_set(&panel->dc, false);
  if (!ret)
    ret = pbl_spi_write(&panel->spi, &cmd, 1);
  if (!ret && len)
    ret = pbl_gpio_set(&panel->dc, true);
  if (!ret && len)
    ret = pbl_spi_write(&panel->spi, data, len);
  return ret;
}

static int prv_wait(const struct uc8253 *panel) {
  return PBL_POLL_SLEEP_MS(pbl_gpio_get(&panel->busy) == 0, 5U, 2000U);
}

static void prv_poll(struct pbl_work *work) {
  struct uc8253_state *state = PBL_CONTAINER_OF(work, struct uc8253_state, poll.work);
  const struct uc8253 *panel = prv_panel(state->disp);
  int ret = pbl_gpio_get(&panel->busy);
  if (ret > 0 && state->elapsed_ms < TIMEOUT_MS) {
    state->elapsed_ms += POLL_MS;
    (void)pbl_delayed_work_schedule(&pbl_sys_workq, &state->poll, PBL_MSEC(POLL_MS));
    return;
  }
  if (ret > 0)
    ret = -ETIMEDOUT;
  if (!ret && state->partial)
    ret = prv_send(panel, PARTIAL_OUT, nullptr, 0);
  if (!ret)
    ret = prv_send(panel, POWER_OFF, nullptr, 0);
  PBL_LOG_DBG("Refresh completed in %u ms (%d)", state->elapsed_ms, ret);
  state->valid = ret == 0;
  if (!ret) {
    memcpy(panel->shown, panel->next, state->disp->props.height * (state->disp->props.width / 8U));
  }
  pbl_display_complete(state->disp, ret);
}

static int prv_init(const struct pbl_display *disp) {
  const struct uc8253 *panel = prv_panel(disp);
  panel->state->disp = disp;
  pbl_delayed_work_init(&panel->state->poll, prv_poll);
  int ret = pbl_gpio_configure(&panel->sd_select, PBL_GPIO_OUTPUT_INACTIVE);
  if (!ret)
    ret = pbl_gpio_configure(&panel->radio_select, PBL_GPIO_OUTPUT_INACTIVE);
  if (!ret)
    ret = pbl_gpio_configure(&panel->dc, PBL_GPIO_OUTPUT_INACTIVE);
  if (!ret)
    ret = pbl_gpio_configure(&panel->busy, PBL_GPIO_INPUT);
  if (!ret)
    ret = pbl_spi_setup(&panel->spi);
  if (ret != 0) {
    PBL_LOG_ERR("Display setup failed (%d)", ret);
  }
  return ret;
}

static int prv_enable(const struct pbl_display *disp) {
  prv_panel(disp)->state->valid = false;
  return 0;
}

static int prv_disable(const struct pbl_display *disp) {
  return 0;
}

static int prv_rotate(const struct pbl_display *disp, bool rotated) {
  prv_panel(disp)->state->rotated = rotated;
  return 0;
}

static int prv_update(const struct pbl_display *disp, const struct pbl_display_buffer *buf,
                      uint16_t y0, uint16_t y1) {
  const struct uc8253 *panel = prv_panel(disp);
  struct uc8253_state *state = panel->state;
  unsigned width = disp->props.width;
  unsigned height = disp->props.height;
  unsigned pitch = width / 8U;
  unsigned left = pitch, right = 0, top = height, bottom = 0;
  for (unsigned y = 0; y < height; ++y) {
    unsigned sy = state->rotated ? height - 1U - y : y;
    for (unsigned col = 0; col < pitch; ++col) {
      uint8_t value = 0;
      for (unsigned bit = 0; bit < 8; ++bit) {
        unsigned x = col * 8U + bit;
        unsigned sx = state->rotated ? width - 1U - x : x;
        value |= ((buf->data[sy * buf->pitch + sx / 8U] >> (sx % 8U)) & 1U) << (7U - bit);
      }
      unsigned pos = y * pitch + col;
      panel->next[pos] = value;
      if (value != panel->shown[pos]) {
        if (col < left)
          left = col;
        if (col > right)
          right = col;
        if (y < top)
          top = y;
        bottom = y;
      }
    }
  }
  if (state->valid && top == height) {
    pbl_display_complete(disp, 0);
    return 0;
  }

  // Newton-re verified software reset on each update and OTP differential mode.
  state->partial = state->valid;
  state->valid = false;
  int ret = prv_wait(panel);
  if (!ret)
    ret = prv_send(panel, PSR, (uint8_t[]){0x1e, 0x0d}, 2);
  if (!ret) {
    pbl_sleep_ms(10);
    ret = prv_wait(panel);
  }
  if (!ret)
    ret = prv_send(panel, PSR, (uint8_t[]){0x1f, 0x0d}, 2);
  if (!ret)
    ret = prv_send(panel, RESOLUTION, (uint8_t[]){width, height >> 8, height}, 3);
  if (!ret)
    ret = prv_send(panel, POWER_ON, nullptr, 0);
  if (!ret)
    ret = prv_wait(panel);
  if (!ret)
    ret = prv_send(panel, CCSET, (uint8_t[]){state->partial ? 2 : 0}, 1);
  if (!ret && state->partial)
    ret = prv_send(panel, TSSET, (uint8_t[]){0x79}, 1);
  if (!ret)
    ret = prv_send(panel, CDI, (uint8_t[]){state->partial ? 0xd7 : 0x97}, 1);
  if (!ret && state->partial) {
    ret = prv_send(panel, PARTIAL_IN, nullptr, 0);
    if (!ret) {
      uint8_t bounds[] = {left * 8U, right * 8U + 7U, top >> 8, top, bottom >> 8, bottom, 1};
      ret = prv_send(panel, PARTIAL_WINDOW, bounds, sizeof(bounds));
    }
  } else {
    left = top = 0;
    right = pitch - 1U;
    bottom = height - 1U;
  }
  for (unsigned plane = 0; !ret && plane < 2; ++plane) {
    const uint8_t *image = plane == 0 && state->partial ? panel->shown : panel->next;
    ret = prv_send(panel, plane == 0 ? OLD_IMAGE : NEW_IMAGE, nullptr, 0);
    if (!ret)
      ret = pbl_gpio_set(&panel->dc, true);
    for (unsigned y = top; !ret && y <= bottom; ++y) {
      ret = pbl_spi_write(&panel->spi, image + y * pitch + left, right - left + 1U);
    }
  }
  if (!ret)
    ret = prv_send(panel, REFRESH, nullptr, 0);
  if (!ret) {
    state->elapsed_ms = POLL_MS;
    (void)pbl_delayed_work_schedule(&pbl_sys_workq, &state->poll, PBL_MSEC(POLL_MS));
  }
  if (ret != 0) {
    PBL_LOG_ERR("Display update failed (%d)", ret);
  }
  return ret;
}

static const struct pbl_display_ops s_ops = {
  .init = prv_init,
  .enable = prv_enable,
  .disable = prv_disable,
  .set_rotated = prv_rotate,
  .update = prv_update,
};

#define PBL_DISPLAY_UC8253_DEFINE(sym, name, bus, cs, hz, dc_pin, busy_pin, sd, radio, w, h)    \
  static_assert((w) == 240 && (h) == 320, "GDEQ031T10 geometry");                               \
  static struct uc8253_state sym##_state;                                                       \
  static uint8_t sym##_shown[(w) / 8 * (h)], sym##_next[(w) / 8 * (h)];                         \
  PBL_DISPLAY_STATE_DEFINE(sym);                                                                \
  PBL_DEVICE_INSTANCE(sym, struct uc8253, struct pbl_display, disp) {                           \
    .disp =                                                                                     \
        PBL_DISPLAY_INIT(sym, name, &(bus)->dev, &s_ops, w, h, PBL_DISPLAY_FORMAT_MONO, false), \
    .spi = PBL_SPI_DEV(bus, cs, hz, 0), .dc = dc_pin, .busy = busy_pin, .sd_select = sd,        \
    .radio_select = radio, .state = &sym##_state, .shown = sym##_shown, .next = sym##_next,     \
  }

#include PBL_DT_INSTANCES(ultrachip_uc8253)
