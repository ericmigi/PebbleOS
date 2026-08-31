# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(USER_CACHE_DIR "${CMAKE_BINARY_DIR}/.cache" CACHE PATH "Zephyr cache directory")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pebbleos_zephyr_gfx)

get_filename_component(PEBBLEOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(GFX "${PEBBLEOS_ROOT}/src/fw/applib/graphics")

target_include_directories(app PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${PEBBLEOS_ROOT}/src/fw"
  "${PEBBLEOS_ROOT}/src/fw/applib/vendor/uPNG"
  "${PEBBLEOS_ROOT}/include"
  "${CMAKE_BINARY_DIR}/include/generated"
)

target_compile_definitions(app PRIVATE
  CONFIG_SCREEN_COLOR_DEPTH_BITS=8
)

if(BOARD STREQUAL "pt2")
  target_compile_definitions(app PRIVATE
    CONFIG_BOARD_OBELIX_PVT=1
    CONFIG_PLATFORM_EMERY=1
    CONFIG_SOC_SF32LB52=1
  )
endif()

target_compile_options(app PRIVATE
  -include string.h
  -Wno-address-of-packed-member
)

target_sources(app PRIVATE
  src/display_push.c
  src/main.c
  src/port_stubs.c

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
  "${PEBBLEOS_ROOT}/lib/util/crc32.c"
  "${PEBBLEOS_ROOT}/lib/util/iterator.c"
  "${PEBBLEOS_ROOT}/lib/util/keyed_circular_cache.c"
  "${PEBBLEOS_ROOT}/lib/util/trig.c"
)

generate_inc_file_for_target(app
  "${PEBBLEOS_ROOT}/resources/normal/base/pbf/LECO_60_NUMBERS_AM_PM.pbf"
  "${CMAKE_BINARY_DIR}/include/generated/leco_60_numbers.pbf.inc"
)
