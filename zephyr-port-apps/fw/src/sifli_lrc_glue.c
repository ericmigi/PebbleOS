/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdarg.h>
#include <stdint.h>

#include <bf0_hal.h>
// bf0_hal.h and Zephyr define IS_ALIGNED with opposite argument order.
#undef IS_ALIGNED
#include <zephyr/kernel.h>

// SiFli's IPC timeout loops use HAL_GetTick(). Its weak default is backed by
// uwTick, which Zephyr does not advance with NONE_HAL_TICK_INIT.
uint32_t HAL_GetTick(void) {
  return k_uptime_get_32();
}

// The weak SiFli implementation derives delays from Zephyr's tickless SysTick
// setup, stretching RF calibration by about 1000x. Use Zephyr's calibrated
// busy-wait so LCPU/RF bring-up completes in milliseconds.
void HAL_Delay_us(uint32_t us) {
  k_busy_wait(us);
}

void HAL_DBG_printf(const char *fmt, ...) {
  (void)fmt;
}

void HAL_Set_backup(uint8_t idx, uint32_t value) {
  volatile uint32_t *backup = &hwp_rtc->BKP0R;
  backup[idx] = value;
}

uint32_t HAL_Get_backup(uint8_t idx) {
  volatile uint32_t *backup = &hwp_rtc->BKP0R;
  return backup[idx];
}
