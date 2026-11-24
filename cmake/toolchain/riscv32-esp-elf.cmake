# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0
#
# Espressif's bare-metal RISC-V toolchain (riscv32-esp-elf). Architecture
# flags are set by cmake/modules/compiler.cmake from Kconfig.
#
# CROSS_COMPILE, given to CMake or in the environment, is the tool prefix,
# a full path one included, e.g.
# ~/.platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-. Without
# it the tools are taken from PATH, then from where PlatformIO and ESP-IDF
# install them.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv32)

if(NOT DEFINED CROSS_COMPILE)
  if(DEFINED ENV{CROSS_COMPILE})
    set(CROSS_COMPILE $ENV{CROSS_COMPILE})
  else()
    set(CROSS_COMPILE riscv32-esp-elf-)
  endif()
endif()

file(GLOB pbl_riscv_hints
  $ENV{HOME}/.platformio/packages/toolchain-riscv32-esp/bin
  $ENV{HOME}/.espressif/tools/riscv32-esp-elf/*/riscv32-esp-elf/bin
)

find_program(CMAKE_C_COMPILER ${CROSS_COMPILE}gcc HINTS ${pbl_riscv_hints} REQUIRED)
# The tools next to the compiler, whatever found it
string(REGEX REPLACE "gcc$" "" CROSS_COMPILE ${CMAKE_C_COMPILER})
set(CROSS_COMPILE ${CROSS_COMPILE} CACHE STRING "Toolchain prefix" FORCE)

find_program(CMAKE_ASM_COMPILER ${CROSS_COMPILE}gcc REQUIRED)
find_program(CMAKE_AR ${CROSS_COMPILE}gcc-ar REQUIRED)
find_program(CMAKE_RANLIB ${CROSS_COMPILE}gcc-ranlib REQUIRED)
find_program(CMAKE_OBJCOPY ${CROSS_COMPILE}objcopy REQUIRED)
find_program(CMAKE_OBJDUMP ${CROSS_COMPILE}objdump REQUIRED)
find_program(CMAKE_SIZE ${CROSS_COMPILE}size)
find_program(CMAKE_NM ${CROSS_COMPILE}nm)

# The firmware supplies its own startup code and links against a linker
# script that is only generated later, so a full link test cannot succeed.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Deterministic archives: no timestamps or UIDs inside .a files.
set(CMAKE_C_ARCHIVE_CREATE "<CMAKE_AR> qcsD <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_C_ARCHIVE_APPEND "<CMAKE_AR> qsD <TARGET> <LINK_FLAGS> <OBJECTS>")
set(CMAKE_C_ARCHIVE_FINISH "<CMAKE_RANLIB> -D <TARGET>")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
