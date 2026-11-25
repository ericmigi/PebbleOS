# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0
#
# The Pebble watch firmware, an application of PebbleOS.
#
#   cmake -B build -GNinja -S pebble -DBOARD=asterix
#   cmake --build build
#
# Pass Kconfig overrides the same way: -DCONFIG_RELEASE=y. See
# docs/development/build_system/index.md.

cmake_minimum_required(VERSION 3.20)

find_package(PebbleOS REQUIRED HINTS ${CMAKE_CURRENT_SOURCE_DIR}/..)
project(pebble LANGUAGES C ASM)

# The SDK platform the firmware offers apps: the one of Kconfig.platform its
# settings for the board pick. Tools are told it by name, and find what it
# is like in tools/pebble_sdk_platform.py.
set(PBL_PLATFORM_NAME ${CONFIG_PLATFORM_NAME})
message(STATUS "Platform: ${PBL_PLATFORM_NAME}")

# The oldest SDK whose apps run: 2.x apps are black and white, and
# rectangular.
if(CONFIG_PLATFORM_COLOR OR CONFIG_PLATFORM_ROUND)
  set(PBL_MIN_SDK_VERSION 3)
else()
  set(PBL_MIN_SDK_VERSION 2)
endif()

# The recovery firmware is selected with -DVARIANT=prf, which loads
# prj_prf.conf; keep the two spellings in sync.
if(VARIANT STREQUAL "prf" AND NOT CONFIG_RECOVERY_FW)
  message(FATAL_ERROR "VARIANT=prf but CONFIG_RECOVERY_FW is not set")
endif()

include(gettext)
include(nanopb)
include(resources)
include(native_sdk)
include(sdk)

# Third-party apps call the firmware through the SDK shims. The recovery
# firmware runs none. A firmware runs them on its CPU when they are of its
# ABI (Cortex-M, RISC-V or Xtensa), and otherwise only with
# Samsa interpreting them. Either way the apps are the watch's, so the SDK to
# build them with can be packaged from all of these.
if(NOT CONFIG_RECOVERY_FW AND (CONFIG_CPU_CORTEX_M OR CONFIG_SAMSA OR CONFIG_APP_NATIVE_ALIASED))
  pbl_native_sdk()
  add_dependencies(pbl_generated_headers pbl_native_sdk)
  set(PBL_FIRMWARE_SOURCES ${PBL_PEBBLE_AUTO_C})
  pbl_sdk()
endif()

# The code most of a UI redraw runs, from RAM: about 20 KiB that covered most of the instructions
# of launcher, Settings and watchface redraws on qemu_esp32c3 (QEMU exec traces). Not memset and
# memcpy: the startup code copies .ramfunc with them.
if(CONFIG_RAMFUNC_DRAWING)
  set(applib "*/CMakeFiles/pebble__applib.dir/graphics")
  set_property(GLOBAL APPEND PROPERTY PBL_RAMFUNC_OBJECTS
    "*libkernel.a:heap.c.obj"
    "*libkernel.a:mutex.c.obj"
    "*liblibutil.a:keyed_circular_cache.c.obj"
    "*liblibutil.a:math.c.obj"
    "${applib}/1_bit/bitblt_private.c.obj"
    "${applib}/1_bit/framebuffer.c.obj"
    "${applib}/bitblt.c.obj"
    "${applib}/framebuffer.c.obj"
    "${applib}/gbitmap.c.obj"
    "${applib}/graphics_line.c.obj"
    "${applib}/graphics_private.c.obj"
    "${applib}/graphics_private_raw.c.obj"
    "${applib}/text_render.c.obj"
    "${applib}/text_resources.c.obj"
    "*/CMakeFiles/pebble__applib.dir/fonts/codepoint.c.obj"
  )
endif()

# Samsa runs the apps built against the SDK generated above.
if(CONFIG_SAMSA)
  add_subdirectory(${PBL_BASE}/subsys/samsa ${CMAKE_BINARY_DIR}/subsys/samsa)
endif()

# The firmware's directories build under pebble/, where the generators and
# the SDK tools expect what they generate.
set(pebble_bin ${CMAKE_BINARY_DIR}/pebble)
file(MAKE_DIRECTORY ${pebble_bin})
pbl_include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${pebble_bin})

set(dirs
  shell
  services
  applib
  mfg
  comm
  debug
  kernel
  popups
  process_management
  process_state
  resource
  syscall
  system
  apps/core
)
if(CONFIG_RECOVERY_FW)
  list(APPEND dirs apps/prf)
else()
  list(APPEND dirs apps)
endif()
foreach(dir ${dirs})
  add_subdirectory(${dir} ${pebble_bin}/${dir})
endforeach()

file(GLOB sources CONFIGURE_DEPENDS *.c)
target_sources(app PRIVATE ${sources})

if(NOT CONFIG_RECOVERY_FW)
  # One universal catalog, independent of board and enabled services.
  file(GLOB_RECURSE pot_sources CONFIGURE_DEPENDS *.c *.h *.def)
  pbl_firmware_pot(${PROJECT_BINARY_DIR}/pebbleos.pot ${pot_sources}
    EXCLUDE apps/prf/** apps/samples/** apps/stored/**)
endif()

# The stored apps are built for the watch's CPU, which the recovery firmware
# runs none on.
if(NOT CONFIG_RECOVERY_FW AND CONFIG_CPU_CORTEX_M)
  add_subdirectory(apps/stored ${pebble_bin}/apps/stored)
endif()

# The bootloader each watch boots the firmware with, flashed ahead of it.
set(bootloader_asterix p2d-bootloader.hex)
set(bootloader_getafix pr2-bootloader.hex)
set(bootloader_obelix pt2-bootloader.hex)
if(bootloader_${PBL_BOARD_NAME})
  pbl_image(bootloader
    HEX ${PBL_BASE}/third_party/bootloaders/pebbleos-bootloaders/${bootloader_${PBL_BOARD_NAME}}
    PARTITION bootloader)
endif()

if(CONFIG_RECOVERY_FW)
  pbl_resources(BUILTIN_ONLY)
else()
  pbl_resources()
endif()
