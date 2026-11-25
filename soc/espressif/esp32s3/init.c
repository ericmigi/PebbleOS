/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>

#include <pbl/console/console.h>
#include <pbl/drivers/watchdog.h>
#include <pbl/kernel/compiler.h>
#include <pbl/kernel/init.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/mmio.h>
#include <pbl/kernel/reboot.h>
#include <pbl/kernel/time.h>

#include <devicetree/regs.h>
#include <hardware/esp32s3_system.h>
#include <hardware/esp32s3_wdt.h>

#define RTC_CNTL(reg) (PBL_DT_REG_RTC_CNTL + ESP32S3_RTC_CNTL_##reg##_OFFSET)
#define SYSTEM(reg)   (PBL_DT_REG_SYSTEM + ESP32S3_SYSTEM_##reg##_OFFSET)

#define XTAL_HZ 40000000U
#define CPU_HZ  PBL_DT_PROP_CPU0_CLOCK_FREQUENCY
#define APB_HZ  PBL_DT_PROP_CLK_APB_CLOCK_FREQUENCY

// ROM entry points (esp32s3.rom.ld)
#define ROM_ETS_INSTALL_PUTC1        ((void (*)(void (*)(char)))0x400005dcU)
#define ROM_ETS_UPDATE_CPU_FREQUENCY ((void (*)(uint32_t ticks_per_us))0x40001a4cU)

#define RTC_STORE_VAL(v) (((v) & 0xffffU) | (((v) & 0xffffU) << 16))

// The APB clock is the CPU clock up to 80 MHz
static_assert(PBL_DT_PROP_CLK_APB_CLOCK_FREQUENCY == (CPU_HZ < 80000000U ? CPU_HZ : 80000000U),
              "clk_apb does not match the CPU clock");

static void prv_timg_wdt_disable(uintptr_t base) {
  pbl_writel(ESP32S3_WDT_WKEY, base + ESP32S3_TIMG_WDTWPROTECT_OFFSET);
  pbl_writel(0U, base + ESP32S3_TIMG_WDTCONFIG0_OFFSET);
  pbl_writel(0U, base + ESP32S3_TIMG_WDTWPROTECT_OFFSET);
}

static void prv_cpu_clock_set(void) {
#if CPU_HZ == 80000000U || CPU_HZ == 160000000U || CPU_HZ == 240000000U
  uint32_t period = (CPU_HZ == 80000000U)
                        ? ESP32S3_SYSTEM_CPUPERIOD_SEL_80M
                        : ((CPU_HZ == 160000000U) ? ESP32S3_SYSTEM_CPUPERIOD_SEL_160M
                                                  : ESP32S3_SYSTEM_CPUPERIOD_SEL_240M);

  pbl_modifyl(
      ESP32S3_SYSTEM_CPU_PER_CONF_CPUPERIOD_SEL_MASK | ESP32S3_SYSTEM_CPU_PER_CONF_PLL_FREQ_SEL,
      PBL_FIELD_PREP(ESP32S3_SYSTEM_CPU_PER_CONF_CPUPERIOD_SEL_MASK, period) |
          ESP32S3_SYSTEM_CPU_PER_CONF_PLL_FREQ_SEL,
      SYSTEM(CPU_PER_CONF));
  pbl_modifyl(ESP32S3_SYSTEM_SYSCLK_CONF_PRE_DIV_CNT_MASK, 0U, SYSTEM(SYSCLK_CONF));
  pbl_modifyl(
      ESP32S3_SYSTEM_SYSCLK_CONF_SOC_CLK_SEL_MASK,
      PBL_FIELD_PREP(ESP32S3_SYSTEM_SYSCLK_CONF_SOC_CLK_SEL_MASK, ESP32S3_SYSTEM_SOC_CLK_SEL_PLL),
      SYSTEM(SYSCLK_CONF));
#elif XTAL_HZ % CPU_HZ == 0U
  pbl_modifyl(
      ESP32S3_SYSTEM_SYSCLK_CONF_PRE_DIV_CNT_MASK | ESP32S3_SYSTEM_SYSCLK_CONF_SOC_CLK_SEL_MASK,
      PBL_FIELD_PREP(ESP32S3_SYSTEM_SYSCLK_CONF_PRE_DIV_CNT_MASK, XTAL_HZ / CPU_HZ - 1U) |
          PBL_FIELD_PREP(ESP32S3_SYSTEM_SYSCLK_CONF_SOC_CLK_SEL_MASK,
                         ESP32S3_SYSTEM_SOC_CLK_SEL_XTAL),
      SYSTEM(SYSCLK_CONF));
#else
#error "cpu0 clock-frequency: 240, 160, 80 or 40 MHz, or 40 MHz divided"
#endif
}

static void prv_rom_putc(char c) {
#ifdef CONFIG_CONSOLE
  if (pbl_in_isr() || pbl_irq_is_locked()) {
    return;
  }
  if (c == '\n') {
    pbl_console_putchar('\r');
  }
  pbl_console_putchar(c);
#else
  (void)c;
#endif
}

void pbl_soc_early_init(void) {
  // Disable ROM-armed watchdogs (TIMG0, TIMG1, RTC WDT, Super WDT)
  prv_timg_wdt_disable(PBL_DT_REG_TIMG0);
  prv_timg_wdt_disable(PBL_DT_REG_TIMG1);

  pbl_writel(ESP32S3_WDT_WKEY, RTC_CNTL(WDTWPROTECT));
  pbl_writel(0U, RTC_CNTL(WDTCONFIG0));
  pbl_writel(0U, RTC_CNTL(WDTWPROTECT));

  pbl_writel(ESP32S3_SWD_WKEY, RTC_CNTL(SWD_WPROTECT));
  pbl_modifyl(0U, ESP32S3_SWD_AUTO_FEED_EN, RTC_CNTL(SWD_CONF));
  pbl_writel(0U, RTC_CNTL(SWD_WPROTECT));

  prv_cpu_clock_set();
  pbl_cycles_per_sec_set(CPU_HZ);
  ROM_ETS_UPDATE_CPU_FREQUENCY(CPU_HZ / 1000000U);
  pbl_writel(RTC_STORE_VAL(XTAL_HZ / 1000000U), RTC_CNTL(STORE4));
  pbl_writel(RTC_STORE_VAL(APB_HZ >> 12), RTC_CNTL(STORE5));

  ROM_ETS_INSTALL_PUTC1(prv_rom_putc);

  watchdog_init();
  watchdog_start();
}

void pbl_soc_reset(void) {
  pbl_writel(ESP32S3_RTC_CNTL_SW_SYS_RST, RTC_CNTL(OPTIONS0));
  for (;;) {
  }
}
