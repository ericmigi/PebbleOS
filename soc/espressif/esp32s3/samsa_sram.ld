/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

/* ESP32-S3 SRAM2 has a free 32 KiB bank when the data cache uses 32 KiB. */
MEMORY
{
    SRAM2 (rw) : ORIGIN = 0x3fcf0000, LENGTH = 0x8000
}

SECTIONS
{
    .samsa_sram (NOLOAD) : ALIGN(16) {
        __samsa_sram_start = .;
        *samsa_runtime.c.obj(.bss.s_app_arena .bss.s_app_scratch)
        __samsa_sram_end = .;
    } > SRAM2
}
ASSERT(SIZEOF(.samsa_sram) == CONFIG_SAMSA_APP_ARENA_SIZE + CONFIG_SAMSA_APP_SCRATCH_SIZE,
       "Samsa app scratch and arena must be in SRAM2")
