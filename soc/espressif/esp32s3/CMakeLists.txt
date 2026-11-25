# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

# The ROM loads the trampoline in IRAM before the linked flash image is accessible.
set(boot_address 0x403c8000)
set(boot_stub ${CMAKE_CURRENT_BINARY_DIR}/boot_stub)
set(boot_header ${CMAKE_CURRENT_BINARY_DIR}/boot_header.S)
set(boot_packer ${PBL_BASE}/tools/cmake/esp32s3_boot_header.py)
math(EXPR flash_mmu_pages "${CONFIG_FLASH_SIZE} / 0x10000")
if(CONFIG_BOARD_XTEINK_X4_PRO AND CONFIG_SAMSA)
  pbl_linker_sources(memory samsa_sram.ld)
  set(cache_flags -DESP32S3_USE_SRAM2)
endif()
add_custom_command(
  OUTPUT ${boot_header}
  BYPRODUCTS ${boot_stub}.elf ${boot_stub}.bin
  COMMAND ${CMAKE_ASM_COMPILER} -DFLASH_MMU_PAGES=${flash_mmu_pages} ${cache_flags} -nostdlib -mtext-section-literals
          -Wl,-Ttext=${boot_address},-e,esp32s3_boot_start,--build-id=none
          ${CMAKE_CURRENT_SOURCE_DIR}/boot_stub.S -o ${boot_stub}.elf
  COMMAND ${CMAKE_OBJCOPY} -O binary -j .text ${boot_stub}.elf ${boot_stub}.bin
  COMMAND ${Python3_EXECUTABLE} ${boot_packer} ${boot_stub}.bin ${boot_header} ${boot_address}
  DEPENDS boot_stub.S ${boot_packer}
  VERBATIM
)

pbl_library()
pbl_include_directories(.)
pbl_library_sources(
  ${boot_header}
  init.c
  intmatrix.c
)
pbl_library_sources_ifdef(CONFIG_CRC retained.c)
pbl_library_sources_ifdef(CONFIG_BOARD_XTEINK_X4_PRO cpu_pll.c power_off.c)
