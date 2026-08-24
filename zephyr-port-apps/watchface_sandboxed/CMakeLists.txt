# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(USER_CACHE_DIR "${CMAKE_BINARY_DIR}/.cache" CACHE PATH "Zephyr cache directory")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pebbleos_zephyr_watchface_sandboxed)

get_filename_component(PEBBLEOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)


option(WATCHFACE_ASCII_PREVIEW "Print an ASCII framebuffer preview on every tick" OFF)

set(GFX "${PEBBLEOS_ROOT}/src/fw/applib/graphics")
string(TIMESTAMP WATCHFACE_BUILD_EPOCH "%s" UTC)

# tick_timer_service.c is copied just as the S1 app does.  Keeping the generated
# copy out of the source tree avoids colliding with PebbleOS's SDK export build.
configure_file(
  "${PEBBLEOS_ROOT}/src/fw/applib/tick_timer_service.c"
  "${CMAKE_CURRENT_BINARY_DIR}/tick_timer_service.c"
  COPYONLY
)

target_include_directories(app PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${PEBBLEOS_ROOT}/zephyr-port-apps/kernel/include"
  "${PEBBLEOS_ROOT}/zephyr-port-apps/gfx/include"
  "${PEBBLEOS_ROOT}/zephyr-port-apps/loader/src"
  "${PEBBLEOS_ROOT}/src/fw"
  "${PEBBLEOS_ROOT}/src/fw/applib/vendor/uPNG"
  "${PEBBLEOS_ROOT}/include"
  "${CMAKE_BINARY_DIR}/include/generated"
)

target_compile_definitions(app PRIVATE
  CONFIG_BOARD_OBELIX_PVT=1
  CONFIG_PLATFORM_EMERY=1
  CONFIG_SCREEN_COLOR_DEPTH_BITS=8
  CONFIG_SOC_SF32LB52=1
  CONFIG_TASK_TIMER_POOL_SIZE=8
  KERNEL_BUILD_EPOCH=${WATCHFACE_BUILD_EPOCH}
  PBL_WTF_CONTEXT=1
)

if(WATCHFACE_ASCII_PREVIEW)
  target_compile_definitions(app PRIVATE WATCHFACE_ASCII_PREVIEW=1)
endif()

target_compile_options(app PRIVATE
  -include string.h
  -Wno-address-of-packed-member
)

target_sources(app PRIVATE
  src/main.c
  src/port.c
  src/sandbox.c
  src/sandbox_bridge.c
  src/sliding_text_emery_resources.c

  # S1: timer spine (the runtime is integration-specific and lives in port.c).
  "${PEBBLEOS_ROOT}/zephyr-port-apps/kernel/src/new_timer_zephyr.c"
  "${PEBBLEOS_ROOT}/lib/os/mutex_zephyr.c"
  "${PEBBLEOS_ROOT}/lib/os/platform_zephyr.c"
  "${PEBBLEOS_ROOT}/lib/os/semaphore_zephyr.c"
  "${PEBBLEOS_ROOT}/lib/os/tick_zephyr.c"
  "${PEBBLEOS_ROOT}/lib/util/list.c"
  "${PEBBLEOS_ROOT}/src/fw/kernel/task_timer.c"
  "${PEBBLEOS_ROOT}/src/fw/services/regular_timer/service.c"
  "${PEBBLEOS_ROOT}/src/fw/services/tick_timer/service.c"
  "${CMAKE_CURRENT_BINARY_DIR}/tick_timer_service.c"

  # S3: real Pebble framebuffer, graphics, text, and UI-layer implementations.
  "${GFX}/8_bit/bitblt_private.c"
  "${GFX}/8_bit/framebuffer.c"
  "${GFX}/arabic_shaping.c"
  "${GFX}/bitblt.c"
  "${GFX}/framebuffer.c"
  "${GFX}/gbitmap.c"
  "${GFX}/gcolor_definitions.c"
  "${GFX}/graphics.c"
  "${GFX}/graphics_circle.c"
  "${GFX}/graphics_line.c"
  "${GFX}/graphics_private.c"
  "${GFX}/graphics_private_raw.c"
  "${GFX}/gtransform.c"
  "${GFX}/gtypes.c"
  "${GFX}/perimeter.c"
  "${GFX}/rtl_support.c"
  "${GFX}/text_layout.c"
  "${GFX}/text_render.c"
  "${GFX}/text_resources.c"
  "${GFX}/utf8.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/fonts/codepoint.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/layer.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/text_layer.c"
  "${PEBBLEOS_ROOT}/lib/util/crc32.c"
  "${PEBBLEOS_ROOT}/lib/util/keyed_circular_cache.c"
  "${PEBBLEOS_ROOT}/lib/util/iterator.c"
  "${PEBBLEOS_ROOT}/lib/util/trig.c"

  # S4: the exact PBW blob and its production checksum implementation.
  "${PEBBLEOS_ROOT}/zephyr-port-apps/loader/src/sliding_text_emery_bin.c"
  "${PEBBLEOS_ROOT}/src/fw/util/legacy_checksum.c"
)

zephyr_linker_sources(ROM_SECTIONS src/sandbox_syscalls.ld)

set_source_files_properties(
  "${PEBBLEOS_ROOT}/src/fw/kernel/task_timer.c"
  "${PEBBLEOS_ROOT}/src/fw/services/regular_timer/service.c"
  PROPERTIES COMPILE_OPTIONS "-include;pbl/drivers/rtc.h"
)

# The app's Gotham fonts come from its embedded Emery resource pack. Keep a
# system Gothic 14 font for TextLayer's default/fallback font.
generate_inc_file_for_target(app
  "${PEBBLEOS_ROOT}/resources/common/base/pbf/GOTHIC_14.pbf"
  "${CMAKE_BINARY_DIR}/include/generated/watchface_font_small.pbf.inc"
)
