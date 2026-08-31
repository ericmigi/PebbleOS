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

void board_init(void) {
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
