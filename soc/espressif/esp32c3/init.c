/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>

#include <pbl/kernel/compiler.h>
#include <pbl/kernel/init.h>
#include <pbl/kernel/mmio.h>
#include <pbl/kernel/reboot.h>
#include <pbl/kernel/time.h>

#include <devicetree/regs.h>
#include <hardware/esp32c3_system.h>
#include <hardware/esp32c3_wdt.h>

#define RTC_CNTL(reg) (PBL_DT_REG_RTC_CNTL + ESP32C3_RTC_CNTL_##reg##_OFFSET)
#define SYSTEM(reg)   (PBL_DT_REG_SYSTEM + ESP32C3_SYSTEM_##reg##_OFFSET)

#define XTAL_HZ 40000000U
#define CPU_HZ  PBL_DT_PROP_CPU0_CLOCK_FREQUENCY

// The APB clock is the CPU's, up to 80 MHz
static_assert(PBL_DT_PROP_CLK_APB_CLOCK_FREQUENCY == (CPU_HZ < 80000000U ? CPU_HZ : 80000000U),
              "clk_apb does not match the CPU clock");

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

/*
 * The CPU clock the devicetree gives cpu0. The ROM leaves the CPU on the crystal halved, and the
 * PLL running at 480 MHz for the USB PHY, which stays as it is: 80 and 160 MHz only switch the CPU
 * over to it, 40 MHz takes the crystal undivided. The APB clock follows the CPU's up to 80 MHz,
 * and the flash's, from the same, to 20 MHz on the PLL.
 */
static void prv_cpu_clock_set(void) {
#if CPU_HZ == 80000000U || CPU_HZ == 160000000U
  pbl_modifyl(
      SYSTEM_CPU_PER_CONF_CPUPERIOD_SEL_MASK,
      PBL_FIELD_PREP(SYSTEM_CPU_PER_CONF_CPUPERIOD_SEL_MASK,
                     CPU_HZ == 80000000U ? SYSTEM_CPUPERIOD_SEL_80M : SYSTEM_CPUPERIOD_SEL_160M),
      SYSTEM(CPU_PER_CONF));
  pbl_modifyl(SYSTEM_SYSCLK_CONF_PRE_DIV_CNT_MASK, 0U, SYSTEM(SYSCLK_CONF));
  pbl_modifyl(SYSTEM_SYSCLK_CONF_SOC_CLK_SEL_MASK,
              PBL_FIELD_PREP(SYSTEM_SYSCLK_CONF_SOC_CLK_SEL_MASK, SYSTEM_SOC_CLK_SEL_PLL),
              SYSTEM(SYSCLK_CONF));
#elif XTAL_HZ % CPU_HZ == 0U
  pbl_modifyl(SYSTEM_SYSCLK_CONF_PRE_DIV_CNT_MASK | SYSTEM_SYSCLK_CONF_SOC_CLK_SEL_MASK,
              PBL_FIELD_PREP(SYSTEM_SYSCLK_CONF_PRE_DIV_CNT_MASK, XTAL_HZ / CPU_HZ - 1U) |
                  PBL_FIELD_PREP(SYSTEM_SYSCLK_CONF_SOC_CLK_SEL_MASK, SYSTEM_SOC_CLK_SEL_XTAL),
              SYSTEM(SYSCLK_CONF));
#else
#error "cpu0 clock-frequency: 160, 80 or 40 MHz, or 40 MHz divided"
#endif
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

  prv_cpu_clock_set();
  // Busy waits count CPU cycles
  pbl_cycles_per_sec_set(CPU_HZ);
}

void pbl_soc_reset(void) {
  pbl_writel(RTC_CNTL_OPTIONS0_SW_SYS_RST, RTC_CNTL(OPTIONS0));
  for (;;) {
  }
}
