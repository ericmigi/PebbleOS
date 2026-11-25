/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <pbl/devicetree.h>
#include <pbl/drivers/backlight.h>
#include <pbl/drivers/pinctrl.h>
#include <pbl/kernel/mmio.h>
#include <pbl/util/misc.h>

#define SYSTEM_CLK_EN0 0x600c0018
#define SYSTEM_RST0    0x600c0020
#define CLK_LEDC       (1U << 11)
#define LEDC_BASE      0x60019000
#define LEDC_TIMER0    (LEDC_BASE + 0xa0)
#define LEDC_CLOCK     (LEDC_BASE + 0xd0)
#define TIMER_UPDATE   (1U << 25)
#define CHANNEL_ENABLE (1U << 2)
#define CHANNEL_UPDATE (1U << 4)
#define DUTY_START     (1U << 31)
#define LEDC_STEPS     1024U

struct frontlight {
  struct pbl_backlight light;
  struct pbl_pinctrl pins;
};

static void prv_channel(unsigned channel, unsigned duty) {
  uintptr_t base = LEDC_BASE + channel * 0x14;
  pbl_writel(0, base + 4); // HPOINT
  pbl_writel(duty << 4, base + 8);
  pbl_writel(DUTY_START | (1U << 20) | (1U << 10), base + 12);
  pbl_writel(CHANNEL_UPDATE | (duty ? CHANNEL_ENABLE : 0), base);
}

static int prv_init(const struct pbl_backlight *light) {
  const struct frontlight *dev = PBL_CONTAINER_OF(light, const struct frontlight, light);
  pbl_modifyl(0, CLK_LEDC, SYSTEM_CLK_EN0);
  pbl_modifyl(CLK_LEDC, 0, SYSTEM_RST0);
  pbl_modifyl(3, 3, LEDC_CLOCK); // XTAL, 40 MHz.
  // At 10-bit resolution, 40 MHz / 25 kHz / 1024 = 400/256.
  pbl_writel(TIMER_UPDATE | (400U << 4) | 10U, LEDC_TIMER0);
  prv_channel(4, 0);
  prv_channel(5, 0);
  return pbl_pinctrl_select_state(&dev->pins, PBL_PINCTRL_STATE_DEFAULT);
}

static int prv_apply(uint32_t color, uint8_t brightness) {
  unsigned total = (LEDC_STEPS - 1U) * brightness / PBL_BACKLIGHT_BRIGHTNESS_MAX;
  unsigned blue = color & 255U;
  // Warm white is the warm emitter endpoint, neutral white is the cool endpoint.
  unsigned cool = blue > 162U ? total * (blue - 162U) / (255U - 162U) : 0;
  prv_channel(4, color ? cool : 0);
  prv_channel(5, color ? total - cool : 0);
  return 0;
}

static int prv_brightness(const struct pbl_backlight *light, uint8_t brightness) {
  return prv_apply(light->state->color, brightness);
}

static int prv_color(const struct pbl_backlight *light, uint32_t color) {
  return prv_apply(color, light->state->brightness);
}

static const struct pbl_backlight_ops s_ops = {
  .init = prv_init,
  .set_brightness = prv_brightness,
  .set_color = prv_color,
};

#define X4PRO_LIGHT_DEFINE(sym, mux)                                         \
  PBL_BACKLIGHT_STATE_DEFINE(sym);                                           \
  PBL_DEVICE_INSTANCE(sym, struct frontlight, struct pbl_backlight, light) { \
    .light = PBL_BACKLIGHT_INIT(sym, "frontlight", nullptr, &s_ops),         \
    .pins = PBL_PINCTRL(mux, nullptr, 0),                                    \
  }

#include PBL_DT_INSTANCES(xteink_x4_pro_frontlight)
