/*
 * SPDX-FileCopyrightText: 2026 Core Devices LLC
 * SPDX-License-Identifier: Apache-2.0
 */

#if CONFIG_RAMFUNC_ALIAS_OFFSET
    /* Linked where the CPU fetches it, and copied to the start of RAM, which .ramfunc_copy
     * reserves. */
#define RAMFUNC_VMA    ORIGIN(RAM) + CONFIG_RAMFUNC_ALIAS_OFFSET
#define RAMFUNC_REGION AT>FLASH
#else
#define RAMFUNC_VMA
#define RAMFUNC_REGION >RAM AT>FLASH
#endif

    /* An MPU region covers it: 32-byte granularity. */
    .ramfunc RAMFUNC_VMA : {
        . = ALIGN(32);
        __ramfunc_start = .;
        *(.ramfunc.text .ramfunc.text.*)
        *(.ramfunc.rodata .ramfunc.rodata.*)
#include <ramfunc-objects.ld>
#include <snippets-ramfunc.ld>
        . = ALIGN(32);
        __ramfunc_end = .;
    } RAMFUNC_REGION
    __ramfunc_size = __ramfunc_end - __ramfunc_start;
    __ramfunc_load_start = LOADADDR(.ramfunc);

#if CONFIG_RAMFUNC_ALIAS_OFFSET
    .ramfunc_copy (NOLOAD) : {
        . += SIZEOF(.ramfunc);
    } >RAM
    __ramfunc_copy_start = ADDR(.ramfunc_copy);
    ASSERT(__ramfunc_copy_start + CONFIG_RAMFUNC_ALIAS_OFFSET == ADDR(.ramfunc),
           "Error: .ramfunc is not first in RAM")
#else
    __ramfunc_copy_start = __ramfunc_start;
#endif
