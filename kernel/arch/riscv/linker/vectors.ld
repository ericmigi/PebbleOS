/*
 * SPDX-FileCopyrightText: 2026 Core Devices LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * All the code, first in FLASH. It runs at its load address plus
 * CONFIG_RISCV_CODE_ALIAS_OFFSET, where a SoC that maps the flash twice has
 * the instruction-bus alias of the data-bus window FLASH is. The read-only
 * data after it, and the load image of .data, stay at FLASH addresses.
 */

#include <ramfunc-exclude.ld>

    .code ORIGIN(FLASH) + CONFIG_RISCV_CODE_ALIAS_OFFSET : {
        KEEP(*(.boot_header))
        KEEP(*(.reset))
        KEEP(*(.vectors))
        /* Not what .ramfunc takes, which comes later */
        *(PBL_RAMFUNC_EXCLUDE .text PBL_RAMFUNC_EXCLUDE .text.*)
        . = ALIGN(16);
    } AT>FLASH
    ASSERT(LOADADDR(.code) == ORIGIN(FLASH), "Error: the code does not start the FLASH region")

    __global_pointer$ = __data_start + 0x800;
