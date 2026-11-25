/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#include <pbl/kernel/compiler.h>

#include <pbl_arch_xtensa.h>

static PBL_ALWAYS_INLINE uint32_t arch_cycle_get_32(void) {
  return xtensa_sr_read(ccount);
}
