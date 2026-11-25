/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>

#include <pbl/console/console.h>
#include <pbl/kernel/compiler.h>
#include <pbl/kernel/init.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/mmio.h>
#include <pbl/kernel/reboot.h>
#include <pbl/kernel/time.h>

#include <devicetree/regs.h>
#include <hardware/esp32c3_gpio.h>
#include <hardware/esp32c3_system.h>
#include <hardware/esp32c3_wdt.h>

#define RTC_CNTL(reg) (PBL_DT_REG_RTC_CNTL + ESP32C3_RTC_CNTL_##reg##_OFFSET)
#define SYSTEM(reg)   (PBL_DT_REG_SYSTEM + ESP32C3_SYSTEM_##reg##_OFFSET)

#define XTAL_HZ 40000000U
#define CPU_HZ  PBL_DT_PROP_CPU0_CLOCK_FREQUENCY
#define APB_HZ  PBL_DT_PROP_CLK_APB_CLOCK_FREQUENCY

// ROM entry points (esp32c3.rom.ld)
#define ROM_ETS_INSTALL_PUTC1        ((void (*)(void (*)(char)))0x40000044U)
#define ROM_ETS_UPDATE_CPU_FREQUENCY ((void (*)(uint32_t ticks_per_us))0x40000588U)
#define ROM_SPIFLASH_CONFIG_CLK      ((int (*)(uint8_t freqdiv, uint8_t spi))0x40000150U)
#define ROM_CACHE_SUSPEND_ICACHE     ((uint32_t (*)(void))0x40000524U)
#define ROM_CACHE_RESUME_ICACHE      ((void (*)(uint32_t autoload))0x40000528U)

// Where ESP-IDF's clock init leaves the crystal and APB frequencies for the ROM and the PHY, as
// a value repeated in both halves
#define ESP32C3_RTC_CNTL_STORE4_OFFSET 0x00b8
#define ESP32C3_RTC_CNTL_STORE5_OFFSET 0x00bc
#define RTC_STORE_VAL(v)               (((v) & 0xffffU) | (((v) & 0xffffU) << 16))

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

#ifdef CONFIG_SOC_ESP32C3_POWER_LATCH
// The level first, then the GPIO matrix, the pad and the enable: the pin never goes low
static void prv_power_latch_hold(void) {
  const uint32_t pin = PBL_DT_PROP_POWER_LATCH_GPIOS;

  pbl_writel(PBL_BIT(pin), PBL_DT_REG_GPIO + ESP32C3_GPIO_OUT_W1TS_OFFSET);
  pbl_writel(GPIO_FUNC_OUT_GPIO, PBL_DT_REG_GPIO + ESP32C3_GPIO_FUNC_OUT_SEL_CFG_OFFSET(pin));
  pbl_modifyl(IO_MUX_MCU_SEL_MASK, PBL_FIELD_PREP(IO_MUX_MCU_SEL_MASK, IO_MUX_FUNC_GPIO),
              PBL_DT_REG_IO_MUX + ESP32C3_IO_MUX_GPIO_OFFSET(pin));
  pbl_writel(PBL_BIT(pin), PBL_DT_REG_GPIO + ESP32C3_GPIO_ENABLE_W1TS_OFFSET);
  // A pad held from before the reset keeps its old level until released
  pbl_modifyl(PBL_BIT(pin), 0U, RTC_CNTL(DIG_PAD_HOLD));
}
#endif

/*
 * The CPU clock the devicetree gives cpu0. The ROM leaves the CPU on the crystal halved, and the
 * PLL running at 480 MHz for the USB PHY, which stays as it is: 80 and 160 MHz only switch the CPU
 * over to it, 40 MHz takes the crystal undivided. The APB clock follows the CPU's up to 80 MHz,
 * and the flash's, from the same, to 20 MHz on the PLL until prv_flash_clock_set().
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

// The ROM prints, and the radio libraries through it, on UART0, whose TX pin a board may use for
// something else: send them to the console. The console may wait for room, so prints from an
// ISR or with interrupts masked, where the radio's timing would suffer, are dropped.
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

#if (CPU_HZ == 80000000U || CPU_HZ == 160000000U) && CONFIG_SOC_ESP32C3_FLASH_CLOCK_DIV != 4
// The clock of the flash the cache fetches through (SPI0), set from RAM with the cache idle, as
// ESP-IDF's bootloader does
PBL_SECTION_RAM static void prv_flash_clock_set(void) {
  uint32_t autoload = ROM_CACHE_SUSPEND_ICACHE();

  ROM_SPIFLASH_CONFIG_CLK(CONFIG_SOC_ESP32C3_FLASH_CLOCK_DIV, 0U);
  ROM_CACHE_RESUME_ICACHE(autoload);
}
#else
static void prv_flash_clock_set(void) {
}
#endif

void pbl_soc_early_init(void) {
#ifdef CONFIG_SOC_ESP32C3_POWER_LATCH
  prv_power_latch_hold();
#endif

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
  prv_flash_clock_set();
  // Busy waits count CPU cycles
  pbl_cycles_per_sec_set(CPU_HZ);
  // So do the ROM's, ets_delay_us() included, at the rate given here
  ROM_ETS_UPDATE_CPU_FREQUENCY(CPU_HZ / 1000000U);
  pbl_writel(RTC_STORE_VAL(XTAL_HZ / 1000000U), RTC_CNTL(STORE4));
  pbl_writel(RTC_STORE_VAL(APB_HZ >> 12), RTC_CNTL(STORE5));

  ROM_ETS_INSTALL_PUTC1(prv_rom_putc);
}

void pbl_soc_reset(void) {
  pbl_writel(RTC_CNTL_OPTIONS0_SW_SYS_RST, RTC_CNTL(OPTIONS0));
  for (;;) {
  }
}
