/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

/** @cond INTERNAL_HIDDEN */

extern volatile uint32_t xtensa_isr_nesting;

static inline bool mcu_state_is_isr(void) {
  return xtensa_isr_nesting != 0U;
}

/** @endcond */
