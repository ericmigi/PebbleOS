/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>

#include <pbl/drivers/watchdog.h>
#include <pbl/kernel/mmio.h>
#include <pbl/util/bits.h>

#include <devicetree/regs.h>
#include <hardware/esp32c3_wdt.h>

// Timer group 0's watchdog, off the 40 MHz crystal at 1 kHz: its one stage resets the whole system,
// from hardware, whatever the CPU does. The RTC watchdog stays the Bluetooth bring-up's.
#define TIMG(reg)   (PBL_DT_REG_TIMG0 + ESP32C3_TIMG_##reg##_OFFSET)
#define RESET_STATE (PBL_DT_REG_RTC_CNTL + ESP32C3_RTC_CNTL_RESET_STATE_OFFSET)
#define XTAL_KHZ    40000U

#define WDT_EN              PBL_BIT(31)
#define WDT_STG0_RESET_SYS  (3U << 29)
#define WDT_CONF_UPDATE_EN  PBL_BIT(22)
#define WDT_USE_XTAL        PBL_BIT(21)
#define WDT_CPU_RST_LEN     (7U << 18)
#define WDT_SYS_RST_LEN     (7U << 15)
#define WDT_CLK_PRESCALE(n) ((uint32_t)(n) << 16)

// Reset causes of the CPU, as the ROM prints them
#define RESET_CAUSE_MASK PBL_GENMASK(5, 0)
#define RESET_POWERON    0x01U
#define RESET_RTC_SW_SYS 0x03U
#define RESET_TG0WDT_SYS 0x07U
#define RESET_TG1WDT_SYS 0x08U
#define RESET_RTCWDT_SYS 0x09U
#define RESET_TG0WDT_CPU 0x0CU
#define RESET_RTC_SW_CPU 0x0DU
#define RESET_RTCWDT_CPU 0x0EU
#define RESET_BROWN_OUT  0x0FU
#define RESET_RTCWDT_RTC 0x10U
#define RESET_TG1WDT_CPU 0x11U
#define RESET_SUPER_WDT  0x12U

static struct pbl_mcu_reboot_reason s_reason;

static void prv_write(uint32_t config0) {
  pbl_writel(WDT_WKEY, TIMG(WDTWPROTECT));
  pbl_writel(config0, TIMG(WDTCONFIG0));
  pbl_writel(config0 | WDT_CONF_UPDATE_EN, TIMG(WDTCONFIG0));
  pbl_writel(0U, TIMG(WDTWPROTECT));
}

void watchdog_init(void) {
  pbl_writel(WDT_WKEY, TIMG(WDTWPROTECT));
  pbl_writel(0U, TIMG(WDTCONFIG0));
  pbl_writel(WDT_CLK_PRESCALE(XTAL_KHZ), TIMG(WDTCONFIG1));
  pbl_writel(CONFIG_WATCHDOG_TIMEOUT_MS, TIMG(WDTCONFIG2));
  pbl_writel(WDT_CONF_UPDATE_EN, TIMG(WDTCONFIG0));
  pbl_writel(0U, TIMG(WDTWPROTECT));
}

void watchdog_start(void) {
  watchdog_feed();
  prv_write(WDT_EN | WDT_STG0_RESET_SYS | WDT_USE_XTAL | WDT_CPU_RST_LEN | WDT_SYS_RST_LEN);
}

void watchdog_stop(void) {
  prv_write(0U);
}

void watchdog_feed(void) {
  pbl_writel(WDT_WKEY, TIMG(WDTWPROTECT));
  pbl_writel(1U, TIMG(WDTFEED));
  pbl_writel(0U, TIMG(WDTWPROTECT));
}

bool watchdog_check_reset_flag(void) {
  return watchdog_clear_reset_flag().independent_watchdog_reset;
}

// The cause stays latched until the next reset: reading it clears nothing
struct pbl_mcu_reboot_reason watchdog_clear_reset_flag(void) {
  uint32_t cause = pbl_readl(RESET_STATE) & RESET_CAUSE_MASK;

  s_reason = (struct pbl_mcu_reboot_reason){
    .power_on_reset = cause == RESET_POWERON,
    .software_reset = cause == RESET_RTC_SW_SYS || cause == RESET_RTC_SW_CPU,
    .brown_out_reset = cause == RESET_BROWN_OUT,
    .independent_watchdog_reset = cause == RESET_TG0WDT_SYS || cause == RESET_TG1WDT_SYS ||
                                  cause == RESET_RTCWDT_SYS || cause == RESET_TG0WDT_CPU ||
                                  cause == RESET_RTCWDT_CPU || cause == RESET_RTCWDT_RTC ||
                                  cause == RESET_TG1WDT_CPU || cause == RESET_SUPER_WDT,
  };

  return s_reason;
}

struct pbl_mcu_reboot_reason watchdog_get_reset_flag(void) {
  return s_reason;
}
