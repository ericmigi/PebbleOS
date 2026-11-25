/*
 * SPDX-FileCopyrightText: 2026 Core Devices LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * All flash-resident code, first in FLASH. It runs at its load address plus
 * CONFIG_XTENSA_CODE_ALIAS_OFFSET (the instruction-bus alias of FLASH).
 * The .vectors table is placed in .ramfunc (IRAM) so window and interrupt
 * handlers work even while the flash cache is suspended.
 */

#include <ramfunc-exclude.ld>

    .code ORIGIN(FLASH) + CONFIG_XTENSA_CODE_ALIAS_OFFSET : {
        KEEP(*(.boot_header))
        KEEP(*(.reset))
        /* Not what .ramfunc takes, which comes later */
        *(PBL_RAMFUNC_EXCLUDE .literal PBL_RAMFUNC_EXCLUDE .literal.*
          PBL_RAMFUNC_EXCLUDE .text PBL_RAMFUNC_EXCLUDE .text.*)
        . = ALIGN(16);
    } AT>FLASH
    ASSERT(LOADADDR(.code) == ORIGIN(FLASH), "Error: the code does not start the FLASH region")
