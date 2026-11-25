/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>

#include <pbl/kernel/irq.h>
#include <pbl/kernel/mmio.h>
#include <pbl/util/bits.h>

#include <devicetree/regs.h>
#include <hardware/esp32s3_intmatrix.h>
#include <hardware/esp32s3_system.h>
#include <pbl_arch_xtensa.h>

/*
 * ESP32-S3 Core 0 interrupt matrix: routes all 99 peripheral sources onto one level-1 external
 * level-triggered Xtensa CPU interrupt (line 2), while disabled sources stay routed to line 16
 * (which is never enabled in INTENABLE). The handler scans the 4 raw status registers masked with
 * the software-tracked enabled set. Software-pending requests raise FROM_CPU0.
 */

#define LINE_KERNEL   2U
#define LINE_DISABLED 16U

#define SOURCE_WORDS 4U

#define STATUS_REG(i) (PBL_DT_REG_INTMATRIX + ESP32S3_INTMATRIX_STATUS_OFFSET(i))
#define FROM_CPU0_REG (PBL_DT_REG_SYSTEM + ESP32S3_SYSTEM_CPU_INTR_FROM_CPU_OFFSET(0))

static uint32_t s_enabled[SOURCE_WORDS];
static uint32_t s_sw_pending[SOURCE_WORDS];

#define IRQ_WORD(irq) ((uint32_t)(irq) / 32U)
#define IRQ_BIT(irq)  PBL_BIT((uint32_t)(irq) % 32U)

static void prv_map(uint32_t source, uint32_t line) {
  pbl_writel(line, PBL_DT_REG_INTMATRIX + ESP32S3_INTMATRIX_MAP_OFFSET(source));
}

void pbl_irq_init(void) {
  xtensa_sr_write(intenable, 0U);
  for (uint32_t source = 0U; source < CONFIG_NUM_IRQS; source++) {
    prv_map(source, LINE_DISABLED);
  }
  pbl_writel(0U, FROM_CPU0_REG);
  prv_map(PBL_SOC_IRQN_FROM_CPU0, LINE_KERNEL);

  xtensa_sr_write(intenable, PBL_BIT(LINE_KERNEL));
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

void pbl_irq_clear_pending(pbl_irq_t irq) {
  unsigned int key = arch_irq_disable_all();

  s_sw_pending[IRQ_WORD(irq)] &= ~IRQ_BIT(irq);
  arch_irq_restore_all(key);
}

void xtensa_irq_dispatch(void) {
  uint32_t pending[SOURCE_WORDS];

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
      if ((s_enabled[i] & bit) == 0U) {
        continue;
      }
      s_sw_pending[i] &= ~bit;
      xtensa_isr_table[n]();
    }
  }
}
