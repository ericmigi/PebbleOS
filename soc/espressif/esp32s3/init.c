/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>
#include <string.h>

#include <pbl/console/console.h>
#include <pbl/drivers/watchdog.h>
#include <pbl/kernel/compiler.h>
#include <pbl/kernel/init.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/mmio.h>
#include <pbl/kernel/reboot.h>
#include <pbl/kernel/section.h>
#include <pbl/kernel/time.h>

#include <devicetree/regs.h>
#include <hardware/esp32c3_gpio.h>
#include <hardware/esp32s3_system.h>
#include <hardware/esp32s3_wdt.h>

#if defined(CONFIG_BOARD_XTEINK_X4_PRO) || defined(CONFIG_BOARD_T_DECK_PRO)
#include "cpu_pll.h"
#endif
#ifdef CONFIG_BOARD_XTEINK_X4_PRO
#include "power_off.h"
#endif

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

static uint32_t prv_cpu_clock_set(void) {
#if CPU_HZ == 240000000U
#if defined(CONFIG_BOARD_XTEINK_X4_PRO) || defined(CONFIG_BOARD_T_DECK_PRO)
  if (!esp32s3_prepare_cpu_pll()) {
    pbl_modifyl(
        ESP32S3_SYSTEM_SYSCLK_CONF_PRE_DIV_CNT_MASK | ESP32S3_SYSTEM_SYSCLK_CONF_SOC_CLK_SEL_MASK,
        0U, SYSTEM(SYSCLK_CONF));
    return XTAL_HZ;
  }
#endif
  pbl_modifyl(
      ESP32S3_SYSTEM_CPU_PER_CONF_CPUPERIOD_SEL_MASK | ESP32S3_SYSTEM_CPU_PER_CONF_PLL_FREQ_SEL,
      PBL_FIELD_PREP(ESP32S3_SYSTEM_CPU_PER_CONF_CPUPERIOD_SEL_MASK,
                     ESP32S3_SYSTEM_CPUPERIOD_SEL_240M) |
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
#error "cpu0 clock-frequency must be supported by the board clock setup"
#endif
  return CPU_HZ;
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

#ifdef CONFIG_SOC_ESP32S3_POWER_LATCH
// Preload the output before enabling it and releasing a hold retained through reset.
static void prv_drive_early_output(uint32_t pin, bool high) {
  const bool low_bank = pin < 32U;
  const uint32_t mask = PBL_BIT(pin % 32U);
  const uint32_t set = low_bank ? ESP32C3_GPIO_OUT_W1TS_OFFSET : ESP32C3_GPIO_OUT1_W1TS_OFFSET;
  const uint32_t clear = low_bank ? ESP32C3_GPIO_OUT_W1TC_OFFSET : ESP32C3_GPIO_OUT1_W1TC_OFFSET;
  pbl_writel(mask, PBL_DT_REG_GPIO + (high ? set : clear));
  pbl_writel(GPIO_FUNC_OUT_GPIO, PBL_DT_REG_GPIO + ESP32C3_GPIO_FUNC_OUT_SEL_CFG_OFFSET(pin));
  pbl_modifyl(IO_MUX_MCU_SEL_MASK, PBL_FIELD_PREP(IO_MUX_MCU_SEL_MASK, IO_MUX_FUNC_GPIO),
              PBL_DT_REG_IO_MUX + ESP32C3_IO_MUX_GPIO_OFFSET(pin));
  pbl_writel(mask, PBL_DT_REG_GPIO + (low_bank ? ESP32C3_GPIO_ENABLE_W1TS_OFFSET
                                               : ESP32C3_GPIO_ENABLE1_W1TS_OFFSET));
  if (pin < 22U) {
    pbl_modifyl(PBL_BIT(pin), 0U, RTC_CNTL(PAD_HOLD));
  } else {
    pbl_modifyl(PBL_BIT(pin - 21U), 0U, RTC_CNTL(DIG_PAD_HOLD));
  }
}
#endif

void pbl_soc_early_init(void) {
#if defined(CONFIG_BOARD_XTEINK_X4_PRO) && defined(CONFIG_SAMSA)
  extern uint8_t __samsa_sram_start[], __samsa_sram_end[];
  memset(__samsa_sram_start, 0, __samsa_sram_end - __samsa_sram_start);
#endif
#ifdef CONFIG_SOC_ESP32S3_POWER_LATCH
  prv_drive_early_output(PBL_DT_PROP_POWER_LATCH_GPIOS, true);
#ifdef CONFIG_BOARD_BOOX_PICCO
  // GPIO0 supplies the panel, sensor bus and SD card together.
  prv_drive_early_output(PBL_DT_PROP_PERIPH_RAIL_GPIOS, false);
#endif
#ifdef CONFIG_BOARD_XTEINK_X4_PRO
  x4_pro_release_sleep_pins();
#endif
#endif

  // Disable ROM-armed watchdogs (TIMG0, TIMG1, RTC WDT, Super WDT)
  prv_timg_wdt_disable(PBL_DT_REG_TIMG0);
  prv_timg_wdt_disable(PBL_DT_REG_TIMG1);

  pbl_writel(ESP32S3_WDT_WKEY, RTC_CNTL(WDTWPROTECT));
  pbl_writel(0U, RTC_CNTL(WDTCONFIG0));
  pbl_writel(0U, RTC_CNTL(WDTWPROTECT));

  pbl_writel(ESP32S3_SWD_WKEY, RTC_CNTL(SWD_WPROTECT));
  pbl_modifyl(0U, ESP32S3_SWD_AUTO_FEED_EN, RTC_CNTL(SWD_CONF));
  pbl_writel(0U, RTC_CNTL(SWD_WPROTECT));

  const uint32_t cpu_hz = prv_cpu_clock_set();
  const uint32_t apb_hz = cpu_hz < 80000000U ? cpu_hz : 80000000U;
  pbl_cycles_per_sec_set(cpu_hz);
  ROM_ETS_UPDATE_CPU_FREQUENCY(cpu_hz / 1000000U);
  pbl_writel(RTC_STORE_VAL(XTAL_HZ / 1000000U), RTC_CNTL(STORE4));
  pbl_writel(RTC_STORE_VAL(apb_hz >> 12), RTC_CNTL(STORE5));

  ROM_ETS_INSTALL_PUTC1(prv_rom_putc);

  watchdog_init();
  watchdog_start();
}

PBL_SECTION_RAM void pbl_soc_reset(void) {
  pbl_writel(ESP32S3_RTC_CNTL_SW_SYS_RST, RTC_CNTL(OPTIONS0));
  for (;;) {
  }
}
