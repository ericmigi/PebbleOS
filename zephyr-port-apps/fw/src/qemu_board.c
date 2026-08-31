/* SPDX-License-Identifier: Apache-2.0 */

// qemu_emery board glue: no SiFli HAL, no LCPU radio, no hardware watchdog.
// Provides the board hooks main.c expects plus the pbl watchdog driver API
// task_watchdog_zephyr.c feeds (the emulated machine has no wdt device, so
// feeding is a no-op and the 60s no-reset criterion holds by construction).

#include <pbl/drivers/watchdog.h>

#include "ble_comm.h"
#include "pbl/logging/logging.h"

void board_early_init(void);
void board_init(void);

void board_early_init(void) {}

// The FreeRTOS reference drives the pebble-display brightness register through
// its backlight PWM model and idles at 100/255; match it so screendumps are
// pixel-comparable (the device scales every RGB channel by brightness/255).
#define QEMU_DISPLAY_BASE 0x40008000u
#define DISP_CTRL 0x000u
#define DISP_BRIGHTNESS 0x018u
#define CTRL_UPDATE_REQUEST (1u << 1)
#define REF_IDLE_BRIGHTNESS 100u

static void prv_match_reference_brightness(void) {
  volatile uint32_t *brightness =
      (volatile uint32_t *)(QEMU_DISPLAY_BASE + DISP_BRIGHTNESS);
  volatile uint32_t *ctrl = (volatile uint32_t *)(QEMU_DISPLAY_BASE + DISP_CTRL);
  *brightness = REF_IDLE_BRIGHTNESS;
  *ctrl |= CTRL_UPDATE_REQUEST;
}

void board_init(void) {
  prv_match_reference_brightness();
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
