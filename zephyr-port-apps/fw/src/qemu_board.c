/* SPDX-License-Identifier: Apache-2.0 */

// qemu_emery board glue: no SiFli HAL, no LCPU radio, no hardware watchdog.
// Provides the board hooks main.c expects plus the pbl watchdog driver API
// task_watchdog_zephyr.c feeds (the emulated machine has no wdt device, so
// feeding is a no-op and the 60s no-reset criterion holds by construction).

#include <stdint.h>

#include <zephyr/kernel.h>

#include <pbl/drivers/watchdog.h>

#include "ble_comm.h"
#include "pbl/logging/logging.h"

void board_early_init(void);
void board_init(void);

void board_early_init(void) {}

// ---------------------------------------------------------------------------
// Backlight: the FreeRTOS reference (CONFIG_BACKLIGHT_QEMU_COLOR) drives the
// pebble-display RGB backlight channel registers and never touches
// DISP_BRIGHTNESS (left at the device's reset default 255). Channel writes do
// not poke CTRL_UPDATE, so backlight activity never lands in frame-recorder
// output; it only affects the live view's ambient/backlight blend.
//
// Behavior mirrors services/light/service.c on that build: boot starts the
// channels at backlight_init's full white and fades out after the standard
// timeout; any button press sets the QEMU pref intensity (100% "Blinding" ->
// channel level 255); when the last button releases, a 3 s timeout is followed
// by a fade ladder (steps of 5 intensity points every 25 ms) down to 0. ALS
// gating and prefs are not ported (qemu has no ALS and prefs default the
// backlight to enabled).
// ---------------------------------------------------------------------------
#define QEMU_DISPLAY_BASE 0x40008000u
#define DISP_BL_RED 0x024u
#define DISP_BL_GREEN 0x028u
#define DISP_BL_BLUE 0x02Cu

// Live user prefs (real shell prefs): intensity is percent (0-100), timeout
// in ms — Battery Saver etc. change these and the light must follow, like the
// reference's light service.
uint8_t backlight_get_intensity(void);
uint32_t backlight_get_timeout_ms(void);
#define LIGHT_INTENSITY_DEFAULT backlight_get_intensity()
#define LIGHT_TIMEOUT_MS backlight_get_timeout_ms()
#define LIGHT_FADE_TIME_MS 500u
#define LIGHT_FADE_MAX_STEPS 20u

static void prv_light_write_channels(uint8_t intensity_pct) {
  const uint8_t level = (uint8_t)(255u * intensity_pct / 100u);
  *(volatile uint32_t *)(QEMU_DISPLAY_BASE + DISP_BL_RED) = level;
  *(volatile uint32_t *)(QEMU_DISPLAY_BASE + DISP_BL_GREEN) = level;
  *(volatile uint32_t *)(QEMU_DISPLAY_BASE + DISP_BL_BLUE) = level;
}

static struct k_timer s_light_timer;
static int s_light_buttons_down;
static uint8_t s_light_brightness;
static bool s_light_fading;

static void prv_light_timer_fn(struct k_timer *timer) {
  // service.c fade ladder: step = ceil(intensity/20); on qemu every level is
  // distinct, so from 100% this walks 95,90,...,5 then 0, one rung per
  // LIGHT_FADE_TIME_MS/20 = 25 ms.
  const uint8_t step = (LIGHT_INTENSITY_DEFAULT + LIGHT_FADE_MAX_STEPS - 1u) /
                       LIGHT_FADE_MAX_STEPS;
  s_light_fading = true;
  s_light_brightness = (s_light_brightness > step) ? (uint8_t)(s_light_brightness - step) : 0u;
  prv_light_write_channels(s_light_brightness);
  if (s_light_brightness > 0u) {
    k_timer_start(&s_light_timer, K_MSEC(LIGHT_FADE_TIME_MS / LIGHT_FADE_MAX_STEPS), K_NO_WAIT);
  } else {
    s_light_fading = false;
  }
}

void fw_light_button_pressed(void) {
  s_light_buttons_down++;
  k_timer_stop(&s_light_timer);
  s_light_fading = false;
  s_light_brightness = LIGHT_INTENSITY_DEFAULT;
  prv_light_write_channels(s_light_brightness);
}

void fw_light_button_released(void) {
  if (s_light_buttons_down > 0) {
    s_light_buttons_down--;
  }
  // The reference's light interaction runs on release too, re-reading the
  // (possibly just-changed) intensity pref — an option-menu pick dims the
  // panel on the same press that chose it.
  if (!s_light_fading && s_light_brightness > 0u) {
    s_light_brightness = LIGHT_INTENSITY_DEFAULT;
    prv_light_write_channels(s_light_brightness);
  }
  if (s_light_buttons_down == 0 && !s_light_fading) {
    k_timer_start(&s_light_timer, K_MSEC(LIGHT_TIMEOUT_MS), K_NO_WAIT);
  }
}

void board_init(void) {
  k_timer_init(&s_light_timer, prv_light_timer_fn, NULL);
  // backlight_init on the reference writes full-white channels at driver init,
  // and the boot interaction starts the standard timeout+fade (the reference
  // idles dark well before user input arrives).
  s_light_brightness = LIGHT_INTENSITY_DEFAULT;
  prv_light_write_channels(s_light_brightness);
  k_timer_start(&s_light_timer, K_MSEC(LIGHT_TIMEOUT_MS), K_NO_WAIT);
  PBL_LOG_ALWAYS("FW_BOARD_DRIVERS_OK");
}

void fw_ble_init(void) {
  PBL_LOG_ALWAYS("FW_BLE_SKIPPED");
}

void watchdog_init(void) {}
void watchdog_start(void) {}
void watchdog_stop(void) {}
void watchdog_feed(void) {}

bool watchdog_check_reset_flag(void) {
  return false;
}

McuRebootReason watchdog_clear_reset_flag(void) {
  return (McuRebootReason){0};
}

McuRebootReason watchdog_get_reset_flag(void) {
  return (McuRebootReason){0};
}
