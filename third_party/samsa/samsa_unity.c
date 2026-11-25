/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

/* The interpreter as one translation unit, so the compiler can inline its
   decoders and helpers into the loop that runs every instruction. */

#include <pbl/kernel/compiler.h>

#ifdef CONFIG_SAMSA_RAMFUNC
// What every instruction runs, without the flash cache in the way
#define SAMSA_HOT PBL_SECTION(".ramfunc.text.samsa")
#endif

#include "samsa/src/alu.c"
#include "samsa/src/decode16.c"
#include "samsa/src/decode32.c"
#include "samsa/src/mem.c"
#include "samsa/src/samsa.c"
