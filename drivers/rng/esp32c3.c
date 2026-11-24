/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>

#include <pbl/drivers/rng.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/mmio.h>
#include <pbl/kernel/time.h>

#include <devicetree/regs.h>

/*
 * The generator adds two bits of entropy every APB cycle from the SAR ADC or the radio, only
 * while one of them is on; otherwise its output is a pseudo-random sequence, good enough for
 * IDs, not for keys. Taking the SAR ADC for it, as the ESP-IDF bootloader does, would fight the
 * ADC driver, so nothing is switched on here. As ESP-IDF's esp_random() does, a word is 16 APB
 * cycles after the last one, XORed with every read in between.
 */
#define APB_CYCLES_PER_WORD 16U

#define CPU_PER_APB (PBL_DT_PROP_CPU0_CLOCK_FREQUENCY / PBL_DT_PROP_CLK_APB_CLOCK_FREQUENCY)

static uint32_t s_last;

int rng_rand(uint32_t *rand_out) {
  uint32_t result = 0U;
  uint32_t now;

  pbl_irq_lock();
  do {
    now = pbl_cycle_get_32();
    result ^= pbl_readl(PBL_DT_REG_RNG);
  } while ((now - s_last) < (CPU_PER_APB * APB_CYCLES_PER_WORD));
  s_last = now;
  pbl_irq_unlock();

  *rand_out = result ^ pbl_readl(PBL_DT_REG_RNG);

  return 0;
}
