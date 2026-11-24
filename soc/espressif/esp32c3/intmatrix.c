/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "intmatrix.h"

#include <errno.h>
#include <stdint.h>

#include <pbl/kernel/irq.h>
#include <pbl/kernel/mmio.h>
#include <pbl/util/bits.h>

#include <devicetree/regs.h>
#include <hardware/esp32c3_intmatrix.h>
#include <hardware/esp32c3_system.h>

/*
 * The interrupt matrix routes each of the 62 sources to one of 31 CPU lines. Every source
 * enabled goes to one line, the others to a line never enabled; the handler reads which sources
 * are up and calls theirs. The matrix's status registers give the sources' raw levels whatever
 * their routing, so the enabled ones are kept here. pbl_irq_set_pending() is done in software,
 * raising FROM_CPU0 to get the line taken. The mie CSR is never touched: the ESP32-C3 has none.
 */

#define INTMATRIX(reg) (PBL_DT_REG_INTMATRIX + ESP32C3_INTMATRIX_##reg##_OFFSET)

// Lines 1 and 6 are those ESP-IDF keeps for Wi-Fi and for disabled sources
#define LINE_KERNEL   2U
#define LINE_DISABLED 6U

#define SOURCE_WORDS 2U

#define STATUS_REG(i) (PBL_DT_REG_INTMATRIX + ESP32C3_INTMATRIX_STATUS_OFFSET(i))

#define FROM_CPU0_REG (PBL_DT_REG_SYSTEM + ESP32C3_SYSTEM_CPU_INTR_FROM_CPU_OFFSET(0))

static uint32_t s_enabled[SOURCE_WORDS];
static uint32_t s_sw_pending[SOURCE_WORDS];

// Handlers attached at runtime, for code that names its sources only then (the radio libraries)
#define ATTACH_SLOTS 4U

static struct {
  pbl_irq_t source;
  void (*fn)(void *arg);
  void *arg;
} s_attached[ATTACH_SLOTS];
static uint32_t s_attached_mask[SOURCE_WORDS];

void arch_irq_spurious(void);

#define IRQ_WORD(irq) ((uint32_t)(irq) / 32U)
#define IRQ_BIT(irq)  PBL_BIT((uint32_t)(irq) % 32U)

static void prv_map(uint32_t source, uint32_t line) {
  pbl_writel(line, PBL_DT_REG_INTMATRIX + ESP32C3_INTMATRIX_MAP_OFFSET(source));
}

void pbl_irq_init(void) {
  pbl_writel(0U, INTMATRIX(ENABLE));
  for (uint32_t source = 0U; source < CONFIG_NUM_IRQS; source++) {
    prv_map(source, LINE_DISABLED);
  }
  pbl_writel(0U, FROM_CPU0_REG);
  prv_map(PBL_SOC_IRQN_FROM_CPU0, LINE_KERNEL);

  // Level-triggered, at the lowest priority the threshold lets through
  pbl_writel(0U, INTMATRIX(TYPE));
  pbl_writel(1U, PBL_DT_REG_INTMATRIX + ESP32C3_INTMATRIX_PRI_OFFSET(LINE_KERNEL));
  pbl_writel(1U, INTMATRIX(THRESH));
  pbl_writel(PBL_BIT(LINE_KERNEL), INTMATRIX(ENABLE));
}

void pbl_irq_enable(pbl_irq_t irq) {
  unsigned int key = arch_irq_disable_all();

  s_enabled[IRQ_WORD(irq)] |= IRQ_BIT(irq);
  prv_map(irq, LINE_KERNEL);
  if ((s_sw_pending[IRQ_WORD(irq)] & IRQ_BIT(irq)) != 0U) {
    pbl_writel(1U, FROM_CPU0_REG);
  }
  arch_irq_restore_all(key);
}

void pbl_irq_disable(pbl_irq_t irq) {
  unsigned int key = arch_irq_disable_all();

  s_enabled[IRQ_WORD(irq)] &= ~IRQ_BIT(irq);
  prv_map(irq, LINE_DISABLED);
  arch_irq_restore_all(key);
}

bool pbl_irq_is_enabled(pbl_irq_t irq) {
  return (s_enabled[IRQ_WORD(irq)] & IRQ_BIT(irq)) != 0U;
}

void pbl_irq_set_pending(pbl_irq_t irq) {
  unsigned int key = arch_irq_disable_all();

  s_sw_pending[IRQ_WORD(irq)] |= IRQ_BIT(irq);
  pbl_writel(1U, FROM_CPU0_REG);
  arch_irq_restore_all(key);
}

// A source that is still up is pending again at once: only the software state can be cleared.
void pbl_irq_clear_pending(pbl_irq_t irq) {
  unsigned int key = arch_irq_disable_all();

  s_sw_pending[IRQ_WORD(irq)] &= ~IRQ_BIT(irq);
  arch_irq_restore_all(key);
}

int esp32c3_irq_attach(pbl_irq_t source, void (*fn)(void *arg), void *arg) {
  int ret = -ENOSPC;

  if (source >= CONFIG_NUM_IRQS || fn == nullptr) {
    return -EINVAL;
  }
  // A line bound at build time keeps its handler
  if (riscv_isr_table[source] != arch_irq_spurious) {
    return -EBUSY;
  }

  unsigned int key = arch_irq_disable_all();

  if ((s_attached_mask[IRQ_WORD(source)] & IRQ_BIT(source)) != 0U) {
    ret = -EBUSY;
  } else {
    for (uint32_t i = 0U; i < ATTACH_SLOTS; i++) {
      if (s_attached[i].fn == nullptr) {
        s_attached[i].source = source;
        s_attached[i].fn = fn;
        s_attached[i].arg = arg;
        s_attached_mask[IRQ_WORD(source)] |= IRQ_BIT(source);
        ret = 0;
        break;
      }
    }
  }
  arch_irq_restore_all(key);

  return ret;
}

void esp32c3_irq_detach(pbl_irq_t source) {
  pbl_irq_disable(source);

  unsigned int key = arch_irq_disable_all();

  for (uint32_t i = 0U; i < ATTACH_SLOTS; i++) {
    if (s_attached[i].fn != nullptr && s_attached[i].source == source) {
      s_attached[i].fn = nullptr;
    }
  }
  s_attached_mask[IRQ_WORD(source)] &= ~IRQ_BIT(source);
  arch_irq_restore_all(key);
}

static void prv_call(uint32_t n) {
  if ((s_attached_mask[IRQ_WORD(n)] & IRQ_BIT(n)) != 0U) {
    for (uint32_t i = 0U; i < ATTACH_SLOTS; i++) {
      if (s_attached[i].fn != nullptr && s_attached[i].source == n) {
        s_attached[i].fn(s_attached[i].arg);
        return;
      }
    }
  }
  riscv_isr_table[n]();
}

void riscv_irq_dispatch(uint32_t cause) {
  uint32_t pending[SOURCE_WORDS];

  (void)cause;

  if ((pbl_readl(STATUS_REG(IRQ_WORD(PBL_SOC_IRQN_FROM_CPU0))) & IRQ_BIT(PBL_SOC_IRQN_FROM_CPU0)) !=
      0U) {
    pbl_writel(0U, FROM_CPU0_REG);
  }

  for (uint32_t i = 0U; i < SOURCE_WORDS; i++) {
    pending[i] = (pbl_readl(STATUS_REG(i)) | s_sw_pending[i]) & s_enabled[i];
  }

  for (uint32_t i = 0U; i < SOURCE_WORDS; i++) {
    while (pending[i] != 0U) {
      const uint32_t bit = pending[i] & -pending[i];
      const uint32_t n = (i * 32U) + (uint32_t)PBL_CTZ(bit);

      pending[i] &= ~bit;
      // An earlier handler may have disabled it: a software request then stays pending
      if ((s_enabled[i] & bit) == 0U) {
        continue;
      }
      s_sw_pending[i] &= ~bit;
      prv_call(n);
    }
  }
}
