# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(USER_CACHE_DIR "${CMAKE_BINARY_DIR}/.cache" CACHE PATH "Zephyr cache directory")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pebbleos_zephyr_ble)

# The SiFli system-config source declares a battery calibration temporary only
# used by an optional vendor configuration path.
zephyr_compile_options(-Wno-unused-variable)

get_filename_component(PEBBLEOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(NIMBLE "${PEBBLEOS_ROOT}/third_party/nimble")
set(MYNEWT_NIMBLE "${NIMBLE}/mynewt-nimble")
set(SIFLI "${PEBBLEOS_ROOT}/third_party/hal_sifli")
set(SIFLI_SDK "${SIFLI}/SiFli-SDK")
set(MBEDTLS "${PEBBLEOS_ROOT}/third_party/mbedtls")

target_include_directories(app BEFORE PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${NIMBLE}/port/include/sf32lb52"
  "${NIMBLE}/port/include"
  "${MYNEWT_NIMBLE}/nimble/include"
  "${MYNEWT_NIMBLE}/nimble/host/include"
  "${MYNEWT_NIMBLE}/nimble/host/services/bas/include"
  "${MYNEWT_NIMBLE}/nimble/host/services/dis/include"
  "${MYNEWT_NIMBLE}/nimble/host/services/gap/include"
  "${MYNEWT_NIMBLE}/nimble/host/services/gatt/include"
  "${MYNEWT_NIMBLE}/nimble/host/util/include"
  "${MYNEWT_NIMBLE}/nimble/transport/include"
  "${MYNEWT_NIMBLE}/nimble/transport/common/hci_h4/include"
  "${MYNEWT_NIMBLE}/porting/nimble/include"
  "${MBEDTLS}/include"
  "${MBEDTLS}/mbedtls/include"
  "${MBEDTLS}/mbedtls/library"
  "${PEBBLEOS_ROOT}/include"
  "${PEBBLEOS_ROOT}/include/pbl"
  "${PEBBLEOS_ROOT}/src/fw"
  "${SIFLI_SDK}/middleware/ipc_queue/porting/os/zephyr"
  "${SIFLI}/sf32lb52"
  "${SIFLI_SDK}/customer/boards/include"
  "${SIFLI_SDK}/drivers/Include"
  "${SIFLI_SDK}/drivers/cmsis/Include"
  "${SIFLI_SDK}/drivers/cmsis/sf32lb52x"
  "${SIFLI_SDK}/external/CMSIS/Include"
  "${SIFLI_SDK}/middleware/include"
  "${SIFLI_SDK}/middleware/ipc_queue/common"
  "${SIFLI_SDK}/middleware/ipc_queue/include"
  "${SIFLI_SDK}/middleware/ipc_queue/porting/sf32lb52x/hcpu"
)

# Appended last so it stays lower precedence than ble/include for the NimBLE
# sources; lets main.c resolve notif_render.h (which the render sources override
# via per-source -I below).
target_include_directories(app PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../notif/include"
)

target_compile_definitions(app PRIVATE
  CONFIG_BOARD_OBELIX_PVT=1
  CONFIG_BOARD_OBELIX=1
  CONFIG_BT_LOG_LEVEL=3
  CONFIG_SOC_SF32LB52=1
  # Folded-in notification render path: select the Emery (pt2) 200x228x8 panel.
  CONFIG_PLATFORM_EMERY=1
  CONFIG_SCREEN_COLOR_DEPTH_BITS=8
  LXT_DISABLE
  MBEDTLS_CONFIG_FILE="pebble_mbedtls_config.h"
  NIMBLE_CFG_CONTROLLER=0
  PEBBLE_BLE_BRINGUP_APP=1
  SF32LB52X_REV_AUTO
  USE_HAL_DRIVER
)

target_compile_options(app PRIVATE
  -Wno-address-of-packed-member
  -Wno-old-style-declaration
  -Wno-pointer-arith
  -Wno-sign-compare
  -Wno-unused-function
  -Wno-unused-variable
)

file(GLOB NIMBLE_PORT_SOURCES
  "${MYNEWT_NIMBLE}/porting/nimble/src/*.c"
  "${MYNEWT_NIMBLE}/nimble/src/*.c"
  "${MYNEWT_NIMBLE}/nimble/host/src/*.c"
  "${MYNEWT_NIMBLE}/nimble/host/util/src/*.c"
  "${MYNEWT_NIMBLE}/nimble/host/services/bas/src/*.c"
  "${MYNEWT_NIMBLE}/nimble/host/services/dis/src/*.c"
  "${MYNEWT_NIMBLE}/nimble/host/services/gap/src/*.c"
  "${MYNEWT_NIMBLE}/nimble/host/services/gatt/src/*.c"
  "${MYNEWT_NIMBLE}/nimble/transport/src/*.c"
  "${MYNEWT_NIMBLE}/nimble/transport/common/hci_h4/src/*.c"
)
file(GLOB MBEDTLS_SOURCES "${MBEDTLS}/mbedtls/library/*.c")
list(REMOVE_ITEM NIMBLE_PORT_SOURCES
  "${MYNEWT_NIMBLE}/porting/nimble/src/hal_timer.c"
  "${MYNEWT_NIMBLE}/nimble/host/src/ble_gatts_lcl.c"
)

set_source_files_properties(
  ${NIMBLE_PORT_SOURCES}
  src/main.c
  "${NIMBLE}/port/src/npl_os_zephyr.c"
  "${PEBBLEOS_ROOT}/src/bluetooth-fw/nimble/advert.c"
  "${PEBBLEOS_ROOT}/src/bluetooth-fw/nimble/id.c"
  PROPERTIES COMPILE_OPTIONS
    "-include;${CMAKE_CURRENT_SOURCE_DIR}/include/nimble_zephyr_compat.h;-Wno-pointer-arith"
)

set_property(SOURCE
  "${MYNEWT_NIMBLE}/nimble/host/src/ble_uuid.c"
  APPEND PROPERTY COMPILE_OPTIONS
    "-include;${CMAKE_CURRENT_SOURCE_DIR}/include/ble_uuid_zephyr_compat.h"
)

target_sources(app PRIVATE
  src/main.c
  src/mbedtls_alloc.c
  src/ppog_min.c
  src/ram_store.c
  src/sifli_lrc_glue.c
  "${PEBBLEOS_ROOT}/lib/os/mutex_zephyr.c"
  "${PEBBLEOS_ROOT}/lib/os/platform_zephyr.c"
  "${PEBBLEOS_ROOT}/lib/os/semaphore_zephyr.c"
  "${PEBBLEOS_ROOT}/lib/os/tick_zephyr.c"
  "${NIMBLE}/port/src/npl_os_zephyr.c"
  "${NIMBLE}/transport/hci_sf32lb52.c"
  "${PEBBLEOS_ROOT}/src/bluetooth-fw/nimble/advert.c"
  "${PEBBLEOS_ROOT}/src/bluetooth-fw/nimble/id.c"
  "${PEBBLEOS_ROOT}/src/bluetooth-fw/nimble/pebble_pairing_service.c"
  "${PEBBLEOS_ROOT}/src/bluetooth-fw/nimble/ppog_reversed_service.c"
  "${PEBBLEOS_ROOT}/src/bluetooth-fw/nimble/nimble_type_conversions.c"
  ${NIMBLE_PORT_SOURCES}
  ${MBEDTLS_SOURCES}

  "${SIFLI_SDK}/drivers/hal/bf0_hal_lcpu_config.c"
  "${SIFLI_SDK}/drivers/hal/bf0_hal_lrc_cal.c"
  "${SIFLI_SDK}/drivers/hal/bf0_hal_lpaon.c"
  "${SIFLI_SDK}/drivers/cmsis/sf32lb52x/bf0_lcpu_init.c"
  "${SIFLI_SDK}/drivers/cmsis/sf32lb52x/bt_rf_fulcal.c"
  "${SIFLI_SDK}/drivers/cmsis/sf32lb52x/lcpu_config_type.c"
  "${SIFLI_SDK}/drivers/cmsis/sf32lb52x/lcpu_patch.c"
  "${PEBBLEOS_ROOT}/third_party/nonfree/pebbleos-nonfree/sf32lb52/lcpu_52x.c"
  "${PEBBLEOS_ROOT}/third_party/nonfree/pebbleos-nonfree/sf32lb52/lcpu_patch_rev_b.c"
  "${SIFLI}/sf32lb52/lcpu_config.c"
  "${SIFLI_SDK}/middleware/ipc_queue/common/circular_buf.c"
  "${SIFLI_SDK}/middleware/ipc_queue/common/ipc_hw.c"
  "${SIFLI_SDK}/middleware/ipc_queue/common/ipc_queue.c"
  "${SIFLI_SDK}/middleware/ipc_queue/porting/sf32lb52x/hcpu/ipc_hw_port.c"
)

# ---------------------------------------------------------------------------
# Folded-in PebbleOS notification render path (shared with zephyr-port-apps/notif).
# These sources need the notif app's include environment, not the BLE one: notif's
# shim headers (kernel/pbl_malloc.h function decls, util/time/time.h, etc.) must
# win over repo include/, src/fw and ble/include. In the app target the BLE
# include dirs are searched first and per-source COMPILE_OPTIONS "-I" land AFTER
# them, so the shims lose. Building the render path as its own static library
# gives it an independent, correctly-ordered include search: notif dirs first
# (via BEFORE), then the Zephyr environment inherited from zephyr_interface.
# ---------------------------------------------------------------------------
set(GFX "${PEBBLEOS_ROOT}/src/fw/applib/graphics")
set(NOTIF "${PEBBLEOS_ROOT}/zephyr-port-apps/notif")

set(RENDER_SOURCES
  "${NOTIF}/src/port.c"
  "${NOTIF}/src/render.c"

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

add_library(notif_render STATIC ${RENDER_SOURCES})
# Inherit the Zephyr compile environment (nostdinc, minimal libc, arch flags,
# autoconf.h) so k_* / zephyr headers resolve, without dragging in the app's
# BLE include dirs.
target_link_libraries(notif_render PRIVATE zephyr_interface)
# notif shim dirs FIRST so they shadow repo include/ + src/fw; this is the exact
# search order the standalone notif app builds with.
target_include_directories(notif_render BEFORE PRIVATE
  "${NOTIF}/include"
  "${PEBBLEOS_ROOT}/zephyr-port-apps/kernel/include"
  "${PEBBLEOS_ROOT}/zephyr-port-apps/gfx/include"
  "${PEBBLEOS_ROOT}/src/fw"
  "${PEBBLEOS_ROOT}/src/fw/applib/vendor/uPNG"
  "${PEBBLEOS_ROOT}/src/fw/applib/vendor/tinflate"
  "${PEBBLEOS_ROOT}/include"
  "${CMAKE_BINARY_DIR}/include/generated"
)
target_compile_definitions(notif_render PRIVATE
  CONFIG_BOARD_OBELIX_PVT=1
  CONFIG_PLATFORM_EMERY=1
  CONFIG_SCREEN_COLOR_DEPTH_BITS=8
  CONFIG_SOC_SF32LB52=1
)
target_compile_options(notif_render PRIVATE
  -include string.h
  -Wno-address-of-packed-member
)
target_link_libraries(app PRIVATE notif_render)

# Fonts + generic-notification icon, mirrored from zephyr-port-apps/notif.
generate_inc_file_for_target(notif_render
  "${PEBBLEOS_ROOT}/resources/normal/base/pbf/GOTHIC_18.pbf"
  "${CMAKE_BINARY_DIR}/include/generated/notif_gothic_18.pbf.inc"
)
generate_inc_file_for_target(notif_render
  "${PEBBLEOS_ROOT}/resources/normal/base/pbf/GOTHIC_24_BOLD.pbf"
  "${CMAKE_BINARY_DIR}/include/generated/notif_gothic_24_bold.pbf.inc"
)
generate_inc_file_for_target(notif_render
  "${PEBBLEOS_ROOT}/resources/normal/base/pbf/GOTHIC_28.pbf"
  "${CMAKE_BINARY_DIR}/include/generated/notif_gothic_28.pbf.inc"
)
generate_inc_file_for_target(notif_render
  "${PEBBLEOS_ROOT}/resources/normal/base/pbf/GOTHIC_28_BOLD.pbf"
  "${CMAKE_BINARY_DIR}/include/generated/notif_gothic_28_bold.pbf.inc"
)
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
generate_inc_file_for_target(notif_render
  "${NOTIF_ICON_PNG}"
  "${CMAKE_BINARY_DIR}/include/generated/notif_generic_icon.png.inc"
)
