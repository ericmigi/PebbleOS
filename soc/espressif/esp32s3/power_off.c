/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "power_off.h"

#include <errno.h>

#include <pbl/kernel/delay.h>
#ifdef CONFIG_SHELL
#include <pbl/shell/shell.h>
#endif
#include <pbl/kernel/mmio.h>
#include <pbl/kernel/reboot.h>
#include <pbl/kernel/section.h>

#include <devicetree/regs.h>
#include <hardware/esp32c3_gpio.h>

#define RTC(offset)  (PBL_DT_REG_RTC_CNTL + (offset))
#define GPIO(offset) (PBL_DT_REG_GPIO + ESP32C3_GPIO_##offset##_OFFSET)
#define RTC_PAD3     0x60008490U
#define RTC_IO_CLK   0x60008904U
#define RTC_MUX      PBL_BIT(19)
#define KEY          PBL_BIT(3)
// GPIO1 is the peripheral rail. GPIO2 and GPIO5 are active-low supplies.
#define HIGH_PADS       (PBL_BIT(1) | PBL_BIT(2) | PBL_BIT(5) | PBL_BIT(14))
#define LOW_PADS        (PBL_BIT(8) | PBL_BIT(9))
#define SLEEP_PADS      (HIGH_PADS | LOW_PADS)
#define PAD_HOLD        RTC(0xd8)
#define DIG_ISO         RTC(0x94)
#define AUTO_HOLD       PBL_BIT(11)
#define CLEAR_AUTO_HOLD PBL_BIT(10)
#define FORCE_UNHOLD    PBL_BIT(14)

#define WAKE_KEY   PBL_BIT(1)
#define WAKE_TIMER PBL_BIT(3)
#ifdef CONFIG_SHELL
static uint32_t s_timer_ticks;
static uint32_t s_reset_cause;
static uint32_t s_wake_cause;

static uint64_t prv_rtc_time(void) {
  pbl_modifyl(0, PBL_BIT(31), RTC(0x0c));
  uint32_t lo = pbl_readl(RTC(0x10));
  return ((uint64_t)(pbl_readl(RTC(0x14)) & 0xffffU) << 32) | lo;
}

static int prv_sleep_test(const struct pbl_shell *sh, size_t argc, char **argv) {
  uint64_t ms = pbl_uptime_ms();
  uint64_t ticks = prv_rtc_time();
  pbl_sleep_ms(50);
  ticks = prv_rtc_time() - ticks;
  ms = pbl_uptime_ms() - ms;
  if (ms == 0 || ms > 500 || ticks < 20 * ms || ticks > 250 * ms) {
    pbl_shell_error(sh, "RTC timer calibration failed");
    return -EIO;
  }
  s_timer_ticks = (uint32_t)(ticks * 5000 / ms);
  pbl_shell_print(sh, "Deep sleep for five seconds or until the power key is pressed");
  pbl_power_off();
}

static int prv_wake_status(const struct pbl_shell *sh, size_t argc, char **argv) {
  pbl_shell_print(sh, "CPU0 reset 0x%x, RTC wake 0x%x (%s)", (unsigned)s_reset_cause,
                  (unsigned)s_wake_cause,
                  s_reset_cause != 5            ? "regular boot"
                  : (s_wake_cause & WAKE_TIMER) ? "timer"
                  : (s_wake_cause & WAKE_KEY)   ? "power key"
                                                : "other");
  return 0;
}
PBL_SHELL_SUBCMD_ADD(sub_sys, sleep_test, nullptr, "Deep sleep with a five-second RTC wake",
                     prv_sleep_test, 1, 0);
PBL_SHELL_SUBCMD_ADD(sub_sys, wake, nullptr, "Show reset and RTC wake causes", prv_wake_status, 1,
                     0);
#endif

static void prv_sleep_levels(void) {
  pbl_writel(HIGH_PADS, GPIO(OUT_W1TS));
  pbl_writel(LOW_PADS, GPIO(OUT_W1TC));
  for (unsigned pin = 0; pin <= 14; pin++) {
    if (SLEEP_PADS & PBL_BIT(pin)) {
      pbl_writel(GPIO_FUNC_OUT_GPIO, PBL_DT_REG_GPIO + ESP32C3_GPIO_FUNC_OUT_SEL_CFG_OFFSET(pin));
      pbl_modifyl(IO_MUX_MCU_SEL_MASK, PBL_FIELD_PREP(IO_MUX_MCU_SEL_MASK, IO_MUX_FUNC_GPIO),
                  PBL_DT_REG_IO_MUX + ESP32C3_IO_MUX_GPIO_OFFSET(pin));
    }
  }
  pbl_writel(SLEEP_PADS, GPIO(ENABLE_W1TS));
}

void x4_pro_release_sleep_pins(void) {
#ifdef CONFIG_SHELL
  s_reset_cause = pbl_readl(RTC(0x38)) & 0x3fU;
  s_wake_cause = pbl_readl(RTC(0x130)) & 0x1ffffU;
#endif
  // Establish safe levels before releasing the output pad latches after wake.
  prv_sleep_levels();
  pbl_modifyl(AUTO_HOLD, CLEAR_AUTO_HOLD | FORCE_UNHOLD, DIG_ISO);
  pbl_modifyl(SLEEP_PADS, 0, PAD_HOLD);
  pbl_modifyl(0, PBL_BIT(31), RTC_IO_CLK);
  pbl_modifyl(RTC_MUX | PBL_BIT(16), 0, RTC_PAD3);
}

static void prv_wait_key_up(enum pbl_sys_off_mode mode, void *user_data) {
  while (!(pbl_readl(GPIO(IN)) & KEY)) {
    pbl_sleep_ms(50);
  }
}

PBL_SYS_OFF_HANDLER_DEFINE(x4_pro_key_release, PBL_SYS_OFF_POWER_OFF_PREPARE, PBL_SYS_OFF_PRIO_LOW,
                           prv_wait_key_up, nullptr);

PBL_SECTION_RAM void pbl_soc_power_off(void) {
  // The display prepare handler has switched its booster off. Keep RESET high.
  prv_sleep_levels();
  pbl_modifyl(0, SLEEP_PADS, PAD_HOLD);
  pbl_modifyl(0, PBL_BIT(31), RTC_IO_CLK);
  // RTC GPIO3 input, pull-up, no pull-down. Enable input during sleep.
  pbl_modifyl(PBL_BIT(28) | PBL_GENMASK(18, 17) | PBL_BIT(14),
              PBL_BIT(27) | RTC_MUX | PBL_BIT(16) | PBL_BIT(15) | PBL_BIT(13), RTC_PAD3);
  pbl_writel(PBL_BIT(22) | KEY, RTC(0xe0)); // Clear old EXT1 cause and select GPIO3.
  pbl_modifyl(PBL_BIT(31), 0, RTC(0x64));   // EXT1_ANY_LOW.
  uint32_t wake = WAKE_KEY;
#ifdef CONFIG_SHELL
  if (s_timer_ticks != 0) {
    const uint64_t alarm = prv_rtc_time() + s_timer_ticks;
    pbl_writel((uint32_t)alarm, RTC(0x04));
    pbl_writel((uint32_t)(alarm >> 32) & 0xffffU, RTC(0x08));
    pbl_writel(PBL_BIT(10), RTC(0x4c));
    pbl_modifyl(0, PBL_BIT(16), RTC(0x08));
    wake |= WAKE_TIMER;
  }
#endif
  pbl_writel(wake << 15, RTC(0x3c));
  pbl_modifyl(PBL_BIT(20) | PBL_BIT(18), 0, RTC(0x88)); // Keep RTC peripheral and pulls on.
  pbl_modifyl(PBL_BIT(31) | PBL_BIT(30) | FORCE_UNHOLD, AUTO_HOLD, DIG_ISO);
  pbl_modifyl(PBL_BIT(20) | PBL_BIT(19), PBL_BIT(31), RTC(0x90)); // Power down digital domain.
  pbl_modifyl(PBL_BIT(13), 0, RTC(0));
  pbl_modifyl(PBL_BIT(31) | PBL_BIT(30) | PBL_BIT(28) | PBL_BIT(27), 0, RTC(0x34));
  pbl_modifyl(PBL_BIT(22), PBL_BIT(21), RTC(0x7c)); // Hand VDD_SPI to the sleep FSM.
  ((void (*)(void (*)(void), size_t))0x4000072cU)(
      nullptr, 0); // esp_rom_set_rtc_wake_addr: cold boot on wake.
  pbl_writel(0, RTC(0x68));
  pbl_writel(3, RTC(0x4c));
  pbl_modifyl(0, PBL_BIT(1) | PBL_BIT(31), RTC(0x18)); // Clear reject cause and enter sleep.
  while (!(pbl_readl(RTC(0x44)) & 3)) {
  }
  pbl_soc_reset(); // A successful deep-sleep wake starts at the ROM reset vector.
}
