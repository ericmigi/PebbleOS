/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>

#include <pbl/drivers/watchdog.h>
#include <pbl/kernel/delay.h>
#include <pbl/kernel/idle.h>
#include <pbl/kernel/init.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/mmio.h>
#include <pbl/kernel/work.h>
#include <pbl/soc/esp32c3/idle.h>
#include <pbl/util/math.h>
#include <pbl/util/size.h>

#ifdef CONFIG_SHELL
#include <pbl/shell/shell.h>
#endif

#include <devicetree/regs.h>
#include <hardware/esp32c3_gpio.h>
#include <hardware/esp32c3_rtc_cntl.h>
#include <hardware/esp32c3_systimer.h>
#include <hardware/esp32c3_wdt.h>

#include "idle_gate.h"

/*
 * Light sleep: the clocks of the digital domain stop until the RTC timer or a pin wakes it, with
 * nothing powered down, the crystal and the PLL included, which the CPU and the flash keep running
 * on. The SYSTIMER stops meanwhile; the RTC timer, on the slow RC oscillator calibrated against
 * the crystal, tells how long it slept, and the SYSTIMER is moved on by that.
 *
 * Never with USB power, nor in the first seconds after boot, both read from the hardware here;
 * never while the RTC watchdog guards something else, and with it armed throughout.
 */

#define RTC(reg)   (PBL_DT_REG_RTC_CNTL + ESP32C3_RTC_CNTL_##reg##_OFFSET)
#define GPIO(reg)  (PBL_DT_REG_GPIO + ESP32C3_GPIO_##reg##_OFFSET)
#define TIMG0(reg) (PBL_DT_REG_TIMG0 + ESP32C3_TIMG_##reg##_OFFSET)
#define SYST(reg)  (PBL_DT_REG_ESP_ESP32C3_SYSTIMER + ESP32C3_SYSTIMER_##reg##_OFFSET)

#define SYSTIMER_UNIT0_LOAD_HI_OFFSET 0x000c
#define SYSTIMER_UNIT0_LOAD_LO_OFFSET 0x0010
#define SYSTIMER_UNIT0_LOAD_OFFSET    0x005c

#define USB_PIN   PBL_DT_PROP_CHARGER_GPIOS
#define LATCH_PIN PBL_DT_PROP_POWER_LATCH_GPIOS

// Longest sleep, well within the watchdogs' timeouts and the 32-bit clock source's wrap
#define MAX_SLEEP_TICKS (10ULL * ESP32C3_SYSTIMER_HZ)
// The RTC watchdog resets the chip when a sleep does not end within this
#define RTC_WDT_S 15U
// Waiting this long for the sleep to start or end, it is taken as rejected: what emulators do
#define SLEEP_WAIT_US 100000U

// The slow clock: cycles counted per calibration, and how often it is calibrated
#define RTC_SLOW_HZ_APPROX 136000U
#define CAL_CYCLES         1024U
#define CAL_PERIOD_MS      30000U
#define XTAL_HZ            40000000U

// ESP-IDF's light sleep settings with the crystal on (rtc.h): digital and RTC at 1.1 V
#define DBIAS_1V10            23U
#define DG_VDD_DRV_B_SLP      254U
#define XTL_BUF_WAIT_SLP_US   250U
#define PLL_BUF_WAIT_SLP      1U
#define CK8M_WAIT_SLP         4U
// The sleep voltages of the digital and RTC regulators, over the analog I2C bus
#define I2C_DIG_REG           0x6dU
#define I2C_DIG_REG_HOSTID    0U
#define I2C_DIG_REG_RTC_SLEEP 5U
#define I2C_DIG_REG_DIG_SLEEP 7U
#define ROM_I2C_WRITE_REG_MASK                                                                \
  ((void (*)(uint8_t block, uint8_t host_id, uint8_t reg, uint8_t msb, uint8_t lsb, uint8_t d)) \
       0x40001960U)

// The RTC watchdog, as third_party/espressif_bt's bring-up guard arms it
#define ESP32C3_RTC_CNTL_WDTCONFIG1_OFFSET 0x0094
#define ESP32C3_RTC_CNTL_WDTFEED_OFFSET    0x00a4
#define RTC_WDT_EN                         PBL_BIT(31)
#define RTC_WDT_STG0_RESET_RTC             (4U << 28)
#define RTC_WDT_CPU_RST_LEN                (7U << 16)
#define RTC_WDT_SYS_RST_LEN                (7U << 13)
#define RTC_WDT_PROCPU_RST_EN              PBL_BIT(11)
#define RTC_WDT_FEED                       PBL_BIT(31)
#define EFUSE_RD_REPEAT_DATA1              0x0034
#define EFUSE_WDT_DELAY_SEL_MASK           PBL_GENMASK(17, 16)

// SYSTIMER ticks per slow clock cycle, 16.16 fixed point; 0 until calibrated
static volatile uint32_t s_slow_period;
static uint32_t s_sleeps;
static uint32_t s_rejects;
static uint32_t s_timeouts;
static uint32_t s_skips;

static uint32_t prv_pins[ESP32C3_GPIO_NUM_PINS];

static uint64_t prv_systimer_now(void) {
  pbl_writel(SYSTIMER_UNIT_OP_UPDATE, SYST(UNIT0_OP));
  (void)PBL_POLL_BUSY_US((pbl_readl(SYST(UNIT0_OP)) & SYSTIMER_UNIT_OP_VALUE_VALID) != 0U, 0U,
                         10U);
  return ((uint64_t)(pbl_readl(SYST(UNIT0_VALUE_HI)) & SYSTIMER_VALUE_HI_MASK) << 32) |
         pbl_readl(SYST(UNIT0_VALUE_LO));
}

// The comparator's target, when the clock event device is armed
static uint64_t prv_systimer_target(void) {
  if ((pbl_readl(SYST(INT_ENA)) & SYSTIMER_INT_TARGET0) == 0U) {
    return UINT64_MAX;
  }
  return ((uint64_t)(pbl_readl(SYST(TARGET0_HI)) & SYSTIMER_VALUE_HI_MASK) << 32) |
         pbl_readl(SYST(TARGET0_LO));
}

// Restarted on a new value; the comparator, reloaded, raises a target already passed at once
static void prv_systimer_resume(uint64_t value) {
  pbl_writel((uint32_t)(value >> 32) & SYSTIMER_VALUE_HI_MASK,
             PBL_DT_REG_ESP_ESP32C3_SYSTIMER + SYSTIMER_UNIT0_LOAD_HI_OFFSET);
  pbl_writel((uint32_t)value, PBL_DT_REG_ESP_ESP32C3_SYSTIMER + SYSTIMER_UNIT0_LOAD_LO_OFFSET);
  pbl_writel(1U, PBL_DT_REG_ESP_ESP32C3_SYSTIMER + SYSTIMER_UNIT0_LOAD_OFFSET);
  pbl_modifyl(SYSTIMER_CONF_TARGET0_WORK_EN, SYSTIMER_CONF_TIMER_UNIT0_WORK_EN, SYST(CONF));
  pbl_writel(SYSTIMER_COMP_LOAD, SYST(COMP0_LOAD));
  pbl_modifyl(0U, SYSTIMER_CONF_TARGET0_WORK_EN, SYST(CONF));
}

static uint64_t prv_rtc_now(void) {
  pbl_writel(RTC_CNTL_TIME_UPDATE, RTC(TIME_UPDATE));
  return ((uint64_t)(pbl_readl(RTC(TIME_HIGH0)) & RTC_CNTL_TIME_HIGH0_MASK) << 32) |
         pbl_readl(RTC(TIME_LOW0));
}

static bool prv_rtc_wdt_armed(void) {
  return (pbl_readl(RTC(WDTCONFIG0)) & RTC_WDT_EN) != 0U;
}

static void prv_rtc_wdt_write(uint32_t config0, uint32_t config1) {
  pbl_writel(WDT_WKEY, RTC(WDTWPROTECT));
  pbl_writel(0U, RTC(WDTCONFIG0));
  pbl_writel(config1, RTC(WDTCONFIG1));
  pbl_writel(RTC_WDT_FEED, RTC(WDTFEED));
  pbl_writel(config0, RTC(WDTCONFIG0));
  pbl_writel(0U, RTC(WDTWPROTECT));
}

// eFuse WDT_DELAY_SEL multiplies stage 0's timeout by 2 to 16
static void prv_rtc_wdt_arm(void) {
  const uint32_t delay_sel =
      PBL_FIELD_GET(EFUSE_WDT_DELAY_SEL_MASK, pbl_readl(PBL_DT_REG_EFUSE + EFUSE_RD_REPEAT_DATA1));

  prv_rtc_wdt_write(RTC_WDT_EN | RTC_WDT_STG0_RESET_RTC | RTC_WDT_CPU_RST_LEN |
                        RTC_WDT_SYS_RST_LEN | RTC_WDT_PROCPU_RST_EN,
                    (RTC_WDT_S * RTC_SLOW_HZ_APPROX) >> (1U + delay_sel));
}

/*
 * Every pin whose interrupt is on and taken on an edge wakes the CPU on the level it does not
 * have now, a change: its interrupt is pending once the edge type is back. USB power wakes it too.
 * The flash's and USB's pins are left alone.
 */
static void prv_pins_wake(void) {
  for (uint32_t pin = 0U; pin < ESP32C3_GPIO_NUM_PINS; pin++) {
    const uintptr_t reg = GPIO(PIN(pin));
    const uint32_t val = pbl_readl(reg);
    const uint32_t type = PBL_FIELD_GET(GPIO_PIN_INT_TYPE_MASK, val);
    const bool high = (pbl_readl(GPIO(IN)) & PBL_BIT(pin)) != 0U;
    bool wake;

    prv_pins[pin] = val;
    if ((pin >= 12U && pin <= 17U) || ESP32C3_GPIO_IS_USB(pin)) {
      continue;
    }
    if (pin == USB_PIN) {
      wake = true;
      high = false;
    } else {
      wake = PBL_FIELD_GET(GPIO_PIN_INT_ENA_MASK, val) != 0U && type >= GPIO_INT_TYPE_RISING &&
             type <= GPIO_INT_TYPE_ANY;
    }
    if (wake) {
      pbl_writel((val & ~GPIO_PIN_INT_TYPE_MASK) |
                     PBL_FIELD_PREP(GPIO_PIN_INT_TYPE_MASK,
                                    high ? GPIO_INT_TYPE_LOW : GPIO_INT_TYPE_HIGH) |
                     GPIO_PIN_WAKEUP_ENABLE,
                 reg);
    }
  }
}

static void prv_pins_restore(void) {
  for (uint32_t pin = 0U; pin < ESP32C3_GPIO_NUM_PINS; pin++) {
    pbl_writel(prv_pins[pin], GPIO(PIN(pin)));
  }
  // Its interrupt is off: nothing to leave pending
  pbl_writel(PBL_BIT(USB_PIN), GPIO(STATUS_W1TC));
}

/*
 * The sleep itself, with every interrupt masked. Returns false if the chip rejected it: a wake
 * source was already up.
 */
static bool prv_light_sleep(uint64_t rtc_ticks) {
  const uint32_t options0 = pbl_readl(RTC(OPTIONS0));
  const uint32_t clk_conf = pbl_readl(RTC(CLK_CONF));
  const uint64_t rtc_start = prv_rtc_now();
  const uint64_t rtc_wake = rtc_start + rtc_ticks;
  const uint64_t sys_start = prv_systimer_now();
  uint32_t raw;
  int ret;

  prv_pins_wake();
  // The battery stays connected whatever the pad does meanwhile
  pbl_modifyl(0U, PBL_BIT(LATCH_PIN), RTC(DIG_PAD_HOLD));

  pbl_writel((uint32_t)rtc_wake, RTC(SLP_TIMER0));
  pbl_writel((uint32_t)(rtc_wake >> 32) & RTC_CNTL_SLP_TIMER1_VAL_HI_MASK, RTC(SLP_TIMER1));
  pbl_writel(RTC_CNTL_INT_MAIN_TIMER, RTC(INT_CLR));
  pbl_modifyl(0U, RTC_CNTL_SLP_TIMER1_MAIN_TIMER_ALARM_EN, RTC(SLP_TIMER1));

  pbl_modifyl(SYSTIMER_CONF_TIMER_UNIT0_WORK_EN, 0U, SYST(CONF));

  pbl_writel(options0 | RTC_CNTL_OPTIONS0_XTL_FORCE_PU | RTC_CNTL_OPTIONS0_BBPLL_FORCE_PU |
                 RTC_CNTL_OPTIONS0_BBPLL_I2C_FORCE_PU | RTC_CNTL_OPTIONS0_BB_I2C_FORCE_PU,
             RTC(OPTIONS0));
  pbl_writel(clk_conf | RTC_CNTL_CLK_CONF_XTAL_GLOBAL_FORCE_NOGATING, RTC(CLK_CONF));

  pbl_modifyl(RTC_CNTL_WAKEUP_ENA_MASK,
              PBL_FIELD_PREP(RTC_CNTL_WAKEUP_ENA_MASK, RTC_CNTL_TRIG_TIMER | RTC_CNTL_TRIG_GPIO),
              RTC(WAKEUP_STATE));
  pbl_modifyl(RTC_CNTL_SLP_REJECT_ENA_MASK,
              PBL_FIELD_PREP(RTC_CNTL_SLP_REJECT_ENA_MASK, RTC_CNTL_TRIG_GPIO),
              RTC(SLP_REJECT_CONF));
  pbl_writel(RTC_CNTL_INT_SLP_WAKEUP | RTC_CNTL_INT_SLP_REJECT, RTC(INT_CLR));
  pbl_modifyl(0U, RTC_CNTL_STATE0_SLP_REJECT_CAUSE_CLR, RTC(STATE0));

  pbl_modifyl(0U, RTC_CNTL_STATE0_SLEEP_EN, RTC(STATE0));
  // The CPU stalls in here while asleep, its cycle counter too
  ret = PBL_POLL_BUSY_US(
      (pbl_readl(RTC(INT_RAW)) & (RTC_CNTL_INT_SLP_WAKEUP | RTC_CNTL_INT_SLP_REJECT)) != 0U, 0U,
      SLEEP_WAIT_US);
  raw = pbl_readl(RTC(INT_RAW));
  pbl_modifyl(RTC_CNTL_STATE0_SLEEP_EN, 0U, RTC(STATE0));
  pbl_writel(RTC_CNTL_INT_SLP_WAKEUP | RTC_CNTL_INT_SLP_REJECT, RTC(INT_CLR));

  pbl_modifyl(RTC_CNTL_WAKEUP_ENA_MASK, 0U, RTC(WAKEUP_STATE));
  pbl_modifyl(RTC_CNTL_SLP_TIMER1_MAIN_TIMER_ALARM_EN, 0U, RTC(SLP_TIMER1));
  pbl_writel(clk_conf, RTC(CLK_CONF));
  pbl_writel(options0, RTC(OPTIONS0));

  // Asleep or not, the RTC timer tells how long the SYSTIMER stood still
  prv_systimer_resume(sys_start + (((prv_rtc_now() - rtc_start) * s_slow_period) >> 16));

  pbl_modifyl(PBL_BIT(LATCH_PIN), 0U, RTC(DIG_PAD_HOLD));
  prv_pins_restore();

  if (ret != 0) {
    s_timeouts++;
    return false;
  }
  return (raw & RTC_CNTL_INT_SLP_WAKEUP) != 0U;
}

static void prv_enter_wfi(void) {
  pbl_cpu_idle();
}

static void prv_enter_light_sleep(void) {
  const uint64_t now = prv_systimer_now();
  const bool usb = (pbl_readl(GPIO(IN)) & PBL_BIT(USB_PIN)) != 0U;
  const uint32_t period = s_slow_period;
  uint64_t ticks;

  ticks = esp32c3_idle_gate(now, prv_systimer_target(), usb, prv_rtc_wdt_armed(), period != 0U,
                            MAX_SLEEP_TICKS);
  if (ticks == 0U) {
    s_skips++;
    pbl_cpu_idle();
    return;
  }

  watchdog_feed();
  prv_rtc_wdt_arm();
  if (prv_light_sleep((ticks << 16) / period)) {
    s_sleeps++;
  } else {
    s_rejects++;
  }
  prv_rtc_wdt_write(0U, 0U);
}

const struct pbl_idle_state pbl_soc_idle_states[] = {
  [SOC_ESP32C3_IDLE_WFI] = {.name = "wfi", .min_ticks = 1, .exit_ticks = 0, .enter = prv_enter_wfi},
  [SOC_ESP32C3_IDLE_LIGHT_SLEEP] =
      {.name = "lightsleep", .min_ticks = 5, .exit_ticks = 1, .enter = prv_enter_light_sleep},
};
const size_t pbl_soc_num_idle_states = PBL_ARRAY_SIZE(pbl_soc_idle_states);

// Crystal cycles in CAL_CYCLES of the slow clock, 0 on a timeout
static uint32_t prv_cal_xtal_cycles(void) {
  uint32_t cycles = 0U;

  pbl_modifyl(TIMG_RTC_CALI_CLK_SEL_MASK | TIMG_RTC_CALI_START_CYCLING | TIMG_RTC_CALI_MAX_MASK,
              PBL_FIELD_PREP(TIMG_RTC_CALI_MAX_MASK, CAL_CYCLES), TIMG0(RTCCALICFG));
  pbl_modifyl(TIMG_RTC_CALI_TIMEOUT_THRES_MASK,
              PBL_FIELD_PREP(TIMG_RTC_CALI_TIMEOUT_THRES_MASK, CAL_CYCLES << 10),
              TIMG0(RTCCALICFG2));
  pbl_modifyl(TIMG_RTC_CALI_START, 0U, TIMG0(RTCCALICFG));
  pbl_modifyl(0U, TIMG_RTC_CALI_START, TIMG0(RTCCALICFG));

  pbl_sleep_ms((CAL_CYCLES * 1000U) / RTC_SLOW_HZ_APPROX + 1U);
  if (PBL_POLL_SLEEP_MS(
          (pbl_readl(TIMG0(RTCCALICFG)) & TIMG_RTC_CALI_RDY) != 0U ||
              (pbl_readl(TIMG0(RTCCALICFG2)) & TIMG_RTC_CALI_TIMEOUT) != 0U,
          1U, 20U) == 0 &&
      (pbl_readl(TIMG0(RTCCALICFG)) & TIMG_RTC_CALI_RDY) != 0U) {
    cycles = PBL_FIELD_GET(TIMG_RTC_CALI_VALUE_MASK, pbl_readl(TIMG0(RTCCALICFG1)));
  }
  pbl_modifyl(TIMG_RTC_CALI_START, 0U, TIMG0(RTCCALICFG));

  return cycles;
}

static void prv_cal(struct pbl_work *work);
static PBL_DELAYED_WORK_DEFINE(s_cal, prv_cal);

// SYSTIMER ticks per slow cycle: 16 MHz from the 40 MHz crystal
static void prv_cal(struct pbl_work *work) {
  const uint32_t cycles = prv_cal_xtal_cycles();

  if (cycles != 0U) {
    s_slow_period =
        (uint32_t)(((uint64_t)cycles * ESP32C3_SYSTIMER_HZ << 16) / ((uint64_t)XTAL_HZ * CAL_CYCLES));
  }
  (void)pbl_delayed_work_reschedule(&pbl_sys_workq, &s_cal, PBL_MSEC(CAL_PERIOD_MS));
}

// Sleep settings, which apply only asleep: nothing powered down, the voltages ESP-IDF uses
static int prv_init(void) {
  const uint32_t slow_cycle_us = 1000000U / RTC_SLOW_HZ_APPROX + 1U;

  pbl_modifyl(RTC_CNTL_DIG_PWC_WIFI_PD_EN | RTC_CNTL_DIG_PWC_BT_PD_EN |
                  RTC_CNTL_DIG_PWC_CPU_TOP_PD_EN | RTC_CNTL_DIG_PWC_DG_PERI_PD_EN |
                  RTC_CNTL_DIG_PWC_DG_WRAP_PD_EN,
              RTC_CNTL_DIG_PWC_LSLP_MEM_FORCE_PU | RTC_CNTL_DIG_PWC_FASTMEM_FORCE_LPU,
              RTC(DIG_PWC));
  pbl_modifyl(RTC_CNTL_BIAS_CONF_DBG_ATTEN_MONITOR | RTC_CNTL_BIAS_CONF_BIAS_SLEEP_MONITOR |
                  RTC_CNTL_BIAS_CONF_PD_CUR_MONITOR | RTC_CNTL_BIAS_CONF_DBG_ATTEN_DEEP_SLP |
                  RTC_CNTL_BIAS_CONF_BIAS_SLEEP_DEEP_SLP | RTC_CNTL_BIAS_CONF_PD_CUR_DEEP_SLP |
                  RTC_CNTL_BIAS_CONF_DG_VDD_DRV_B_SLP_MASK,
              RTC_CNTL_BIAS_CONF_DG_VDD_DRV_B_SLP_EN |
                  PBL_FIELD_PREP(RTC_CNTL_BIAS_CONF_DG_VDD_DRV_B_SLP_MASK, DG_VDD_DRV_B_SLP),
              RTC(BIAS_CONF));
  pbl_irq_lock();
  ROM_I2C_WRITE_REG_MASK(I2C_DIG_REG, I2C_DIG_REG_HOSTID, I2C_DIG_REG_RTC_SLEEP, 4U, 0U,
                         DBIAS_1V10);
  ROM_I2C_WRITE_REG_MASK(I2C_DIG_REG, I2C_DIG_REG_HOSTID, I2C_DIG_REG_DIG_SLEEP, 4U, 0U,
                         DBIAS_1V10);
  pbl_irq_unlock();
  pbl_modifyl(0U, RTC_CNTL_REG_REGULATOR_FORCE_PU, RTC(REG));
  // VDD_SPI feeds the flash and the power latch's pin
  pbl_modifyl(RTC_CNTL_SDIO_CONF_PD_EN, 0U, RTC(SDIO_CONF));
  pbl_modifyl(0U, RTC_CNTL_SLP_REJECT_LIGHT_EN | RTC_CNTL_SLP_REJECT_DEEP_EN,
              RTC(SLP_REJECT_CONF));
  pbl_modifyl(RTC_CNTL_TIMER1_PLL_BUF_WAIT_MASK | RTC_CNTL_TIMER1_XTL_BUF_WAIT_MASK |
                  RTC_CNTL_TIMER1_CK8M_WAIT_MASK,
              PBL_FIELD_PREP(RTC_CNTL_TIMER1_PLL_BUF_WAIT_MASK, PLL_BUF_WAIT_SLP) |
                  PBL_FIELD_PREP(RTC_CNTL_TIMER1_XTL_BUF_WAIT_MASK,
                                 PBL_DIV_ROUND_UP(XTL_BUF_WAIT_SLP_US, slow_cycle_us)) |
                  PBL_FIELD_PREP(RTC_CNTL_TIMER1_CK8M_WAIT_MASK, CK8M_WAIT_SLP),
              RTC(TIMER1));

  (void)pbl_delayed_work_reschedule(&pbl_sys_workq, &s_cal, PBL_NO_WAIT);

  return 0;
}

PBL_SYS_INIT(prv_init, POST_KERNEL, 0);

#ifdef CONFIG_SHELL
static int prv_cmd_lightsleep(const struct pbl_shell *sh, size_t argc, char **argv) {
  pbl_shell_print(sh, "sleeps %" PRIu32 " rejected %" PRIu32 " timed out %" PRIu32
                      " skipped %" PRIu32 " slow period %" PRIu32 "/65536 ticks",
                  s_sleeps, s_rejects, s_timeouts, s_skips, s_slow_period);
  return 0;
}

PBL_SHELL_SUBCMD_ADD(sub_sys, lightsleep, nullptr, "Light sleep counts", prv_cmd_lightsleep, 1, 0);
#endif
