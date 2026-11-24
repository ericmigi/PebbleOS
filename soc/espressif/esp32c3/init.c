/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>

#include <pbl/kernel/compiler.h>
#include <pbl/kernel/init.h>
#include <pbl/kernel/mmio.h>
#include <pbl/kernel/reboot.h>
#include <pbl/kernel/time.h>

#include <devicetree/regs.h>
#include <hardware/esp32c3_wdt.h>

#define RTC_CNTL(reg) (PBL_DT_REG_RTC_CNTL + ESP32C3_RTC_CNTL_##reg##_OFFSET)

// What the ROM looks for at the start of the flash to jump to 0x42000008 with the flash mapped
PBL_SECTION(".boot_header")
PBL_USED const uint32_t esp32c3_direct_boot_magic[2] = {
  0xaedb041dU,
  0xaedb041dU,
};

static void prv_timg_wdt_disable(uintptr_t base) {
  pbl_writel(WDT_WKEY, base + ESP32C3_TIMG_WDTWPROTECT_OFFSET);
  pbl_writel(0U, base + ESP32C3_TIMG_WDTCONFIG0_OFFSET);
  pbl_writel(0U, base + ESP32C3_TIMG_WDTWPROTECT_OFFSET);
}

void pbl_soc_early_init(void) {
  // The ROM may leave the timer group and RTC watchdogs armed, and the super watchdog always is
  prv_timg_wdt_disable(PBL_DT_REG_TIMG0);
  prv_timg_wdt_disable(PBL_DT_REG_TIMG1);

  pbl_writel(WDT_WKEY, RTC_CNTL(WDTWPROTECT));
  pbl_writel(0U, RTC_CNTL(WDTCONFIG0));
  pbl_writel(0U, RTC_CNTL(WDTWPROTECT));

  pbl_writel(SWD_WKEY, RTC_CNTL(SWD_WPROTECT));
  pbl_modifyl(0U, SWD_AUTO_FEED_EN, RTC_CNTL(SWD_CONF));
  pbl_writel(0U, RTC_CNTL(SWD_WPROTECT));

  // The clocks stay where the ROM left them; busy waits count CPU cycles
  pbl_cycles_per_sec_set(PBL_DT_PROP_CPU0_CLOCK_FREQUENCY);
}

void pbl_soc_reset(void) {
  pbl_writel(RTC_CNTL_OPTIONS0_SW_SYS_RST, RTC_CNTL(OPTIONS0));
  for (;;) {
  }
}
