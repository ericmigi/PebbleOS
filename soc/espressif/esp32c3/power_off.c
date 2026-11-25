/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>

#include <pbl/kernel/mmio.h>
#include <pbl/kernel/reboot.h>

#include <devicetree/regs.h>
#include <hardware/esp32c3_gpio.h>

#define GPIO(reg) (PBL_DT_REG_GPIO + ESP32C3_GPIO_##reg##_OFFSET)

#define LATCH_PIN PBL_DT_PROP_POWER_LATCH_GPIOS
#define USB_PIN   PBL_DT_PROP_CHARGER_GPIOS

static bool prv_usb_present(void) {
  return (pbl_readl(GPIO(IN)) & PBL_BIT(USB_PIN)) != 0U;
}

/*
 * The latch low disconnects the battery, but the power button bridges it while held and USB powers
 * the board regardless. The rail goes once the button is up; USB plugged in meanwhile resets, as
 * does the watchdog if the rail stays.
 */
[[noreturn]] void pbl_soc_power_off(void) {
  if (!prv_usb_present()) {
    // A held pad would ignore the new level
    pbl_modifyl(PBL_BIT(LATCH_PIN), 0U,
                PBL_DT_REG_RTC_CNTL + ESP32C3_RTC_CNTL_DIG_PAD_HOLD_OFFSET);
    pbl_writel(PBL_BIT(LATCH_PIN), GPIO(OUT_W1TC));
    while (!prv_usb_present()) {
    }
  }

  pbl_soc_reset();
}
