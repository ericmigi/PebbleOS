/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>

#include <pbl/drivers/timer.h>
#include <pbl/kernel/delay.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/mmio.h>

#include <devicetree/regs.h>
#include <hardware/esp32c3_systimer.h>

/*
 * Unit 0 of the SYSTIMER, a 52-bit counter of a fixed 16 MHz, is the clock source, read 32 bits
 * at a time; comparator 0 on it, one shot, the clock event device. A value loaded while the
 * comparator works only matches the counter reaching it, so one already passed would never raise
 * the event; loaded with the comparator stopped, it is compared once it starts again, and a
 * value passed raises the event at once.
 */

#define SYSTIMER_BASE PBL_DT_REG_ESP_ESP32C3_SYSTIMER

// Target 0 is the first interrupt of the block
#define SYSTIMER_IRQN PBL_SOC_IRQN_SYSTIMER_TARGET0

// A few APB cycles
#define SNAPSHOT_TIMEOUT_US 10U

#define MIN_DELTA 32U
#define MAX_DELTA (1U << 30)

static inline uint32_t prv_getreg(uint32_t offset) {
  return pbl_readl(SYSTIMER_BASE + offset);
}

static inline void prv_putreg(uint32_t offset, uint32_t value) {
  pbl_writel(value, SYSTIMER_BASE + offset);
}

// The counter is read through a snapshot the block takes on request
static uint64_t prv_now(void) {
  prv_putreg(ESP32C3_SYSTIMER_UNIT0_OP_OFFSET, SYSTIMER_UNIT_OP_UPDATE);
  (void)PBL_POLL_BUSY_US(
      (prv_getreg(ESP32C3_SYSTIMER_UNIT0_OP_OFFSET) & SYSTIMER_UNIT_OP_VALUE_VALID) != 0U, 0U,
      SNAPSHOT_TIMEOUT_US);

  return ((uint64_t)(prv_getreg(ESP32C3_SYSTIMER_UNIT0_VALUE_HI_OFFSET) & SYSTIMER_VALUE_HI_MASK)
          << 32) |
         prv_getreg(ESP32C3_SYSTIMER_UNIT0_VALUE_LO_OFFSET);
}

static uint32_t prv_read(const struct pbl_clocksource *cs) {
  return (uint32_t)prv_now();
}

static void prv_set_next(const struct pbl_clockevent *ce, uint32_t delta) {
  const uint64_t target = prv_now() + delta;

  pbl_modifyl(SYSTIMER_CONF_TARGET0_WORK_EN, 0U, SYSTIMER_BASE + ESP32C3_SYSTIMER_CONF_OFFSET);
  prv_putreg(ESP32C3_SYSTIMER_INT_CLR_OFFSET, SYSTIMER_INT_TARGET0);
  prv_putreg(ESP32C3_SYSTIMER_TARGET0_HI_OFFSET, (uint32_t)(target >> 32) & SYSTIMER_VALUE_HI_MASK);
  prv_putreg(ESP32C3_SYSTIMER_TARGET0_LO_OFFSET, (uint32_t)target);
  prv_putreg(ESP32C3_SYSTIMER_COMP0_LOAD_OFFSET, SYSTIMER_COMP_LOAD);
  pbl_modifyl(0U, SYSTIMER_CONF_TARGET0_WORK_EN, SYSTIMER_BASE + ESP32C3_SYSTIMER_CONF_OFFSET);
  prv_putreg(ESP32C3_SYSTIMER_INT_ENA_OFFSET, SYSTIMER_INT_TARGET0);
}

static void prv_shutdown(const struct pbl_clockevent *ce) {
  prv_putreg(ESP32C3_SYSTIMER_INT_ENA_OFFSET, 0U);
  prv_putreg(ESP32C3_SYSTIMER_INT_CLR_OFFSET, SYSTIMER_INT_TARGET0);
}

static int prv_init(const struct pbl_device *dev);

static const struct pbl_clocksource_ops s_cs_ops = {
  .read = prv_read,
};

static const struct pbl_clockevent_ops s_ce_ops = {
  .set_next = prv_set_next,
  .shutdown = prv_shutdown,
};

PBL_DEVICE_STATE_DEFINE(s_systimer_cs);
static const struct pbl_clocksource s_systimer_cs =
    PBL_CLOCKSOURCE_INIT(s_systimer_cs, "systimer", prv_init, nullptr, &s_cs_ops, 32);

PBL_DEVICE_STATE_DEFINE(s_systimer_ce);
static const struct pbl_clockevent s_systimer_ce = PBL_CLOCKEVENT_INIT(
    s_systimer_ce, "systimer", prv_init, nullptr, &s_ce_ops, MIN_DELTA, MAX_DELTA);

const struct pbl_clocksource *const pbl_system_clocksource = &s_systimer_cs;
const struct pbl_clockevent *const pbl_system_clockevent = &s_systimer_ce;

static void prv_isr(void) {
  if ((prv_getreg(ESP32C3_SYSTIMER_INT_ST_OFFSET) & SYSTIMER_INT_TARGET0) == 0U) {
    return;
  }

  // One shot: the comparator would match again at once
  prv_putreg(ESP32C3_SYSTIMER_INT_ENA_OFFSET, 0U);
  prv_putreg(ESP32C3_SYSTIMER_INT_CLR_OFFSET, SYSTIMER_INT_TARGET0);
  pbl_clockevent_handler(&s_systimer_ce);
}

PBL_IRQ_CONNECT(SYSTIMER_TARGET0, CONFIG_TIMER_ESP32C3_SYSTIMER_IRQ_PRIO, prv_isr, , 0);

// Both devices share the timer.
static int prv_init(const struct pbl_device *dev) {
  static bool s_started;

  if (!s_started) {
    // Comparator 0 one shot on unit 0, which runs out of reset
    prv_putreg(ESP32C3_SYSTIMER_INT_ENA_OFFSET, 0U);
    prv_putreg(ESP32C3_SYSTIMER_TARGET0_CONF_OFFSET, 0U);
    pbl_modifyl(
        0U,
        SYSTIMER_CONF_CLK_EN | SYSTIMER_CONF_TIMER_UNIT0_WORK_EN | SYSTIMER_CONF_TARGET0_WORK_EN,
        SYSTIMER_BASE + ESP32C3_SYSTIMER_CONF_OFFSET);
    prv_putreg(ESP32C3_SYSTIMER_INT_CLR_OFFSET, SYSTIMER_INT_TARGET0);
    pbl_irq_enable(PBL_IRQN(SYSTIMER_TARGET0));
    s_started = true;
  }

  if (dev == &s_systimer_cs.dev) {
    pbl_clocksource_set_hz(&s_systimer_cs, ESP32C3_SYSTIMER_HZ);
  } else {
    pbl_clockevent_set_hz(&s_systimer_ce, ESP32C3_SYSTIMER_HZ);
  }

  return 0;
}
