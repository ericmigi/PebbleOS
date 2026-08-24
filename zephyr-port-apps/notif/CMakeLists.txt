# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(USER_CACHE_DIR "${CMAKE_BINARY_DIR}/.cache" CACHE PATH "Zephyr cache directory")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pebbleos_zephyr_notif)

get_filename_component(PEBBLEOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(GFX "${PEBBLEOS_ROOT}/src/fw/applib/graphics")

target_include_directories(app PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${PEBBLEOS_ROOT}/zephyr-port-apps/kernel/include"
  "${PEBBLEOS_ROOT}/zephyr-port-apps/gfx/include"
  "${PEBBLEOS_ROOT}/src/fw"
  "${PEBBLEOS_ROOT}/src/fw/applib/vendor/uPNG"
  "${PEBBLEOS_ROOT}/src/fw/applib/vendor/tinflate"
  "${PEBBLEOS_ROOT}/include"
  "${CMAKE_BINARY_DIR}/include/generated"
)

target_compile_definitions(app PRIVATE
  CONFIG_BOARD_OBELIX_PVT=1
  CONFIG_PLATFORM_EMERY=1
  CONFIG_SCREEN_COLOR_DEPTH_BITS=8
  CONFIG_SOC_SF32LB52=1
)

target_compile_options(app PRIVATE
  -include string.h
  -Wno-address-of-packed-member
)

target_sources(app PRIVATE
  src/main.c
  src/port.c

  # PebbleOS notification card and its shared timeline/text-node machinery.
  "${PEBBLEOS_ROOT}/src/fw/services/timeline/notification_layout.c"
  "${PEBBLEOS_ROOT}/src/fw/services/timeline/layout_layer.c"
  "${PEBBLEOS_ROOT}/src/fw/services/timeline/layout_node.c"
  "${PEBBLEOS_ROOT}/src/fw/services/timeline/timeline_layout.c"
  "${PEBBLEOS_ROOT}/src/fw/apps/system/timeline/text_node.c"

  # Real Pebble layer, framebuffer, graphics, and text implementations.
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/layer.c"
  "${GFX}/8_bit/bitblt_private.c"
  "${GFX}/8_bit/framebuffer.c"
  "${GFX}/arabic_shaping.c"
  "${GFX}/bitblt.c"
  "${GFX}/framebuffer.c"
  "${GFX}/gbitmap.c"
  "${GFX}/gbitmap_png.c"
  "${GFX}/gcolor_definitions.c"
  "${GFX}/graphics.c"
  "${GFX}/graphics_bitmap.c"
  "${GFX}/graphics_circle.c"
  "${GFX}/graphics_line.c"
  "${GFX}/graphics_private.c"
  "${GFX}/graphics_private_raw.c"
  "${GFX}/gtransform.c"
  "${GFX}/gtypes.c"
  "${GFX}/gpath.c"
  "${GFX}/perimeter.c"
  "${GFX}/rtl_support.c"
  "${GFX}/text_layout.c"
  "${GFX}/text_render.c"
  "${GFX}/text_resources.c"
  "${GFX}/utf8.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/vendor/uPNG/upng.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/vendor/tinflate/tinflate.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/fonts/codepoint.c"
  "${PEBBLEOS_ROOT}/lib/util/crc32.c"
  "${PEBBLEOS_ROOT}/lib/util/hash.c"
  "${PEBBLEOS_ROOT}/lib/util/keyed_circular_cache.c"
  "${PEBBLEOS_ROOT}/lib/util/iterator.c"
  "${PEBBLEOS_ROOT}/lib/util/trig.c"
  "${PEBBLEOS_ROOT}/lib/util/math.c"
)

generate_inc_file_for_target(app
  "${PEBBLEOS_ROOT}/resources/normal/base/pbf/GOTHIC_18.pbf"
  "${CMAKE_BINARY_DIR}/include/generated/notif_gothic_18.pbf.inc"
)

# Use PebbleOS's production generic-notification artwork. The firmware resource
# build normally converts this SVG to PDC; this standalone app converts it to a
# palette PNG so the same artwork can go through the real GBitmap path.
find_program(MAGICK_EXECUTABLE magick REQUIRED)
set(NOTIF_ICON_PNG "${CMAKE_CURRENT_BINARY_DIR}/notification_generic_25.png")
add_custom_command(
  OUTPUT "${NOTIF_ICON_PNG}"
  COMMAND "${MAGICK_EXECUTABLE}"
          "${PEBBLEOS_ROOT}/resources/normal/base/images/Pebble_25x25_Generic_notification.svg"
          -background none -resize 25x25 -type Palette "PNG8:${NOTIF_ICON_PNG}"
  DEPENDS
          "${PEBBLEOS_ROOT}/resources/normal/base/images/Pebble_25x25_Generic_notification.svg"
  VERBATIM
)
generate_inc_file_for_target(app
  "${NOTIF_ICON_PNG}"
  "${CMAKE_BINARY_DIR}/include/generated/notif_generic_icon.png.inc"
)
generate_inc_file_for_target(app
  "${PEBBLEOS_ROOT}/resources/normal/base/pbf/GOTHIC_24_BOLD.pbf"
  "${CMAKE_BINARY_DIR}/include/generated/notif_gothic_24_bold.pbf.inc"
)
generate_inc_file_for_target(app
  "${PEBBLEOS_ROOT}/resources/normal/base/pbf/GOTHIC_28.pbf"
  "${CMAKE_BINARY_DIR}/include/generated/notif_gothic_28.pbf.inc"
)
generate_inc_file_for_target(app
  "${PEBBLEOS_ROOT}/resources/normal/base/pbf/GOTHIC_28_BOLD.pbf"
  "${CMAKE_BINARY_DIR}/include/generated/notif_gothic_28_bold.pbf.inc"
)
