/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

.section .text, "ax"
.global esp32s3_boot_start
esp32s3_boot_start:
    j 0f
    .balign 4
    .literal_position
0:
    movi a2, 0x00040020
    wsr a2, ps
    movi a2, 0
    wsr a2, windowbase
    rsync
    movi a2, 1
    wsr a2, windowstart
    rsync
    movi a1, 0x3fcc0000

    movi a4, 0x4000186c /* Cache_Disable_ICache */
    callx4 a4
    movi a4, 0x40001884 /* Cache_Disable_DCache */
    callx4 a4
    movi a4, 0x40001998 /* Cache_MMU_Init */
    callx4 a4

    /* Map the configured 64 KiB flash pages at IROM 0x42000000 and DROM 0x3c000000. */
    movi a2, 0x600c5000
    movi a3, 0
    movi a4, FLASH_MMU_PAGES
1:
    s32i a3, a2, 0
    addi a2, a2, 4
    addi a3, a3, 1
    blt a3, a4, 1b

    movi a4, 0x40001650 /* Cache_Set_Default_Mode */
    callx4 a4
#ifdef ESP32S3_USE_SRAM2
    /* SRAM2 at 0x3fcf0000 is available with a 32 KiB data cache. */
    movi a6, 32768
    movi a7, 4
    movi a8, 16
    movi a4, 0x40001a28 /* rom_config_data_cache_mode */
    callx4 a4
#endif
    movi a4, 0x400016d4 /* Cache_Invalidate_ICache_All */
    callx4 a4
    movi a4, 0x400016e0 /* Cache_Invalidate_DCache_All */
    callx4 a4
    movi a6, 0
    movi a4, 0x40001878 /* Cache_Enable_ICache(0) */
    callx4 a4
    movi a6, 0
    movi a4, 0x40001890 /* Cache_Enable_DCache(0) */
    callx4 a4

    /* Unshut Core 0 DCache (0x600c4004) and ICache (0x600c4064) buses */
    movi a2, 0x600c4004
    movi a3, 0
    s32i a3, a2, 0
    movi a2, 0x600c4064
    s32i a3, a2, 0
    isync

    /* Allocate the instruction-cache SRAM bank and set 16 KiB, 4 ways, 32-byte lines.
     * Enabling the controller alone leaves the instruction bus unusable on silicon. */
    movi a6, 16384
    movi a7, 4
    movi a8, 32
    movi a4, 0x40001a1c /* rom_config_instruction_cache_mode */
    callx4 a4
    memw
    isync
    movi a2, 0x42001000
    jx a2
