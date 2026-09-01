# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(USER_CACHE_DIR "${CMAKE_BINARY_DIR}/.cache" CACHE PATH "Zephyr cache directory")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pebbleos_zephyr_fw)

option(FW_OTA_TEST_INJECT "Write a non-executable OTA test image at boot" OFF)

# The SiFli system-config source declares a battery calibration temporary only
# used by an optional vendor configuration path.
zephyr_compile_options(-Wno-unused-variable)

get_filename_component(PEBBLEOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
string(TIMESTAMP FW_BUILD_EPOCH "%s" UTC)
set(GFX "${PEBBLEOS_ROOT}/src/fw/applib/graphics")
set(SIFLI "${PEBBLEOS_ROOT}/third_party/hal_sifli")
set(SIFLI_SDK "${SIFLI}/SiFli-SDK")
set(NIMBLE "${PEBBLEOS_ROOT}/third_party/nimble")
set(MYNEWT_NIMBLE "${NIMBLE}/mynewt-nimble")
set(MBEDTLS "${PEBBLEOS_ROOT}/third_party/mbedtls")

# pt2 == real SF32LB52 hardware: SiFli HAL, shipping QSPI flash stack, NimBLE
# host + LCPU controller. Any other board (qemu_emery) gets Zephyr-native
# flash/input/rtc backends and no radio.
if(BOARD STREQUAL "pt2")
  set(FW_BOARD_PT2 TRUE)
else()
  set(FW_BOARD_PT2 FALSE)
endif()

if(FW_BOARD_PT2)
# Keep the generated NimBLE syscfg and its Zephyr compatibility shim ahead of
# PebbleOS's general include roots.
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
  "${SIFLI_SDK}/middleware/ipc_queue/porting/os/zephyr"
  "${SIFLI_SDK}/middleware/ipc_queue/common"
  "${SIFLI_SDK}/middleware/ipc_queue/include"
  "${SIFLI_SDK}/middleware/ipc_queue/porting/sf32lb52x/hcpu"
)

# SiFli HAL + CMSIS, for the shipping QSPI flash driver (src/fw/drivers/*).
target_include_directories(app PRIVATE
  "${SIFLI}/sf32lb52"
  "${SIFLI_SDK}/drivers/Include"
  "${SIFLI_SDK}/drivers/cmsis/Include"
  "${SIFLI_SDK}/drivers/cmsis/sf32lb52x"
  "${SIFLI_SDK}/external/CMSIS/Include"
  "${SIFLI_SDK}/customer/boards/include"
  "${SIFLI_SDK}/middleware/include"
)
endif()

target_include_directories(app PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${PEBBLEOS_ROOT}/zephyr-port-apps/watchface_sandboxed/include"
  "${PEBBLEOS_ROOT}/zephyr-port-apps/loader/src"
  "${PEBBLEOS_ROOT}/include"
  "${PEBBLEOS_ROOT}/src/fw"
  "${PEBBLEOS_ROOT}/src/fw/applib/vendor/uPNG"
  "${PEBBLEOS_ROOT}/src/fw/applib/vendor/tinflate"
  "${PEBBLEOS_ROOT}/zephyr-port-apps/kernel/include"
  "${PEBBLEOS_ROOT}/zephyr-port-apps/gfx/include"
  "${CMAKE_BINARY_DIR}/include/generated"

  "${PEBBLEOS_ROOT}/include/pbl"
)

if(FW_BOARD_PT2)
target_compile_definitions(app PRIVATE
  CONFIG_FLASH_GD25Q256E=1
  CONFIG_BOARD_OBELIX=1
  CONFIG_BOARD_OBELIX_PVT=1
  CONFIG_BT_LOG_LEVEL=3
  CONFIG_SOC_SF32LB52=1

  # NimBLE host + the external SF32 LCPU controller.
  LXT_DISABLE
  MBEDTLS_CONFIG_FILE="pebble_mbedtls_config.h"
  NIMBLE_CFG_CONTROLLER=0

  # SiFli HAL configuration for the shipping QSPI flash driver.
  USE_HAL_DRIVER
  SOC_BF0_HCPU
  SF32LB52X
  SF32LB52X_REV_AUTO
  HAL_TICK_PER_SECOND=1024
)
endif()

target_compile_definitions(app PRIVATE
  CONFIG_PLATFORM_EMERY=1
  CONFIG_SCREEN_COLOR_DEPTH_BITS=8
  CONFIG_PBLBOOT=1
  CONFIG_FIRMWARE_SLOT=0
  CONFIG_PEBBLE_ZEPHYR_CORE_BOOT=1
  CONFIG_RELEASE=1
  CONFIG_TASK_TIMER_POOL_SIZE=16
  CONFIG_SERVICE_EVENT_SERVICE_LOG_LEVEL=0
  CONFIG_SERVICE_ANALYTICS_LOG_LEVEL=0
  CONFIG_SERVICE_FILESYSTEM_LOG_LEVEL=0
  CONFIG_SERVICE_NEW_TIMER_LOG_LEVEL=0
  CONFIG_SERVICE_REGULAR_TIMER_LOG_LEVEL=0
  CONFIG_SERVICE_SYSTEM_TASK_LOG_LEVEL=0
  CONFIG_SERVICE_TICK_TIMER_LOG_LEVEL=0
  FW_BUILD_EPOCH=${FW_BUILD_EPOCH}
  ANALYTICS_ZEPHYR=1
  PBL_WTF_CONTEXT=1
)

if(FW_OTA_TEST_INJECT)
  target_compile_definitions(app PRIVATE FW_OTA_TEST_INJECT=1)
endif()

target_compile_options(app PRIVATE
  -include string.h
  -Wno-address-of-packed-member
  -Wno-old-style-declaration
  -Wno-pointer-arith
  -Wno-sign-compare
  -Wno-unused-function
  -Wno-unused-variable
)

if(FW_BOARD_PT2)
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
  src/ble_comm.c
  src/ppog_min.c
  src/ram_store.c
  src/sprf_bond.c
  "${NIMBLE}/port/src/npl_os_zephyr.c"
  "${PEBBLEOS_ROOT}/src/bluetooth-fw/nimble/pebble_pairing_service.c"
  "${PEBBLEOS_ROOT}/src/bluetooth-fw/nimble/ppog_reversed_service.c"
  "${PEBBLEOS_ROOT}/src/bluetooth-fw/nimble/nimble_type_conversions.c"
  PROPERTIES COMPILE_OPTIONS
    "-include;${CMAKE_CURRENT_SOURCE_DIR}/include/nimble_zephyr_compat.h;-Wno-pointer-arith"
)

set_property(SOURCE
  src/ram_store.c
  src/sprf_bond.c
  APPEND PROPERTY COMPILE_OPTIONS -fshort-enums
)

set_property(SOURCE
  "${MYNEWT_NIMBLE}/nimble/host/src/ble_uuid.c"
  APPEND PROPERTY COMPILE_OPTIONS
    "-include;${CMAKE_CURRENT_SOURCE_DIR}/include/ble_uuid_zephyr_compat.h"
)

# pt2-only sources: real board glue, shipping QSPI flash stack, NimBLE host,
# SF32 LCPU bring-up, and the minimal PPoGATT/PutBytes OTA path riding on it.
target_sources(app PRIVATE
  src/ble_comm.c
  src/button_raw_pt2.c
  src/flash_shim.c
  src/qspi_board.c
  src/ppog_min.c
  src/putbytes_min.c
  src/ram_store.c
  src/sprf_bond.c
  src/sifli_lrc_glue.c

  "${PEBBLEOS_ROOT}/src/fw/board/boards/board_obelix_zephyr.c"
  "${PEBBLEOS_ROOT}/src/fw/drivers/watchdog/zephyr.c"
  "${PEBBLEOS_ROOT}/lib/btutil/bt_uuid.c"
  "${NIMBLE}/port/src/npl_os_zephyr.c"
  "${NIMBLE}/transport/hci_sf32lb52.c"
  "${PEBBLEOS_ROOT}/src/bluetooth-fw/nimble/pebble_pairing_service.c"
  "${PEBBLEOS_ROOT}/src/bluetooth-fw/nimble/ppog_reversed_service.c"
  "${PEBBLEOS_ROOT}/src/bluetooth-fw/nimble/nimble_type_conversions.c"
  ${NIMBLE_PORT_SOURCES}
  ${MBEDTLS_SOURCES}

  # SiFli LCPU boot, RF calibration, patch, and HCPU/LCPU IPC transport.
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

  # Shipping PebbleOS flash stack (flash_impl_* over the SiFli QSPI HAL). Same
  # driver obelix ships, replacing the Zephyr flash driver that could not persist
  # writes/erases at the filesystem region offset.
  "${PEBBLEOS_ROOT}/src/fw/drivers/flash/gd25q256e.c"
  "${PEBBLEOS_ROOT}/src/fw/drivers/sf32lb52/qspi.c"
  "${PEBBLEOS_ROOT}/lib/os/mcu/cache_arm.c"
  "${SIFLI_SDK}/drivers/hal/bf0_hal_mpi.c"
  "${SIFLI_SDK}/drivers/hal/bf0_hal_mpi_ex.c"
  "${SIFLI_SDK}/drivers/hal/bf0_hal_dma.c"
  "${SIFLI_SDK}/drivers/hal/flash_table.c"
)
else()
# qemu_emery: Zephyr-native backends (pebble,extflash flash, input-subsystem
# buttons, no radio, no hardware watchdog).
target_sources(app PRIVATE
  src/button_raw_qemu.c
  src/flash_shim_qemu.c
  src/qemu_board.c
)
# The real src/fw/flash_region/flash_region.h resolves its chip layout from
# this; the qemu layout matches the FreeRTOS SPI image (and the app's shadow
# flash_region header).
# FW_REAL_SHELL: boot into TicToc + the REAL launcher app (fw_shell.c) instead
# of the custom launcher menu; needs the real system resource pack, which qemu
# has in its FreeRTOS-layout SPI flash image (resource_pack.c).
target_compile_definitions(app PRIVATE CONFIG_FLASH_QEMU=1 FW_REAL_SHELL=1)
# Generated resource IDs (resource/resource_ids.auto.h + the timeline resource
# table) come from the FreeRTOS reference build of the same tree. BEFORE so the
# real generated IDs beat the gfx app's minimal resource-id shadow.
target_include_directories(app BEFORE PRIVATE "${PEBBLEOS_ROOT}/build/src/fw")
endif()

target_sources(app PRIVATE
  src/app_registry.c
  src/app_registry_stubs.c
  src/app_service_stubs.c
  src/app_alarm_stubs.c
  src/app_ui_window_stubs.c
  src/button_zephyr.c
  src/input_service.c
  src/launcher_ui.c
  src/ui_shims.c
  src/freertos_compat.c
  src/fw_ota_boot.c
  src/main.c
  src/pfs_boot.c
  src/port.c
  src/sandbox_launcher.c
  src/system_app.c
  src/wall_clock_zephyr.c

  "${PEBBLEOS_ROOT}/src/fw/drivers/task_watchdog_zephyr.c"
  "${PEBBLEOS_ROOT}/src/fw/services/analytics/analytics.c"
  "${PEBBLEOS_ROOT}/src/fw/services/analytics/zephyr.c"

  # Privileged built-in system apps: real PebbleOS app code launched via their
  # PebbleProcessMd through fw_system_app_launch(). Add new system apps here; see
  # zephyr-port-notes/SYSTEM-APPS-BUILDOUT.md.
  "${PEBBLEOS_ROOT}/src/fw/apps/watch/tictoc/tictoc.c"
  "${PEBBLEOS_ROOT}/src/fw/apps/watch/tictoc/default/tictoc_default.c"
  "${PEBBLEOS_ROOT}/src/fw/apps/system/music.c"
  "${PEBBLEOS_ROOT}/src/fw/apps/system/alarms/alarms.c"
  "${PEBBLEOS_ROOT}/src/fw/apps/system/alarms/alarm_editor.c"
  "${PEBBLEOS_ROOT}/src/fw/apps/system/alarms/alarm_detail.c"

  # Watchfaces picker + Settings shell (real app code) and their port backends
  # (prefs/i18n/app_menu_data_source/status_bar/settings-submodule stubs).
  src/apps_port_glue.c
  "${PEBBLEOS_ROOT}/src/fw/apps/system/watchfaces.c"
  "${PEBBLEOS_ROOT}/src/fw/apps/system/settings/settings.c"
  "${PEBBLEOS_ROOT}/src/fw/apps/system/settings/menu.c"
  "${PEBBLEOS_ROOT}/src/fw/apps/system/settings/window.c"
  "${PEBBLEOS_ROOT}/src/fw/apps/system/settings/health.c"
  # (menu_layer_system_cells.c is added next to menu_layer.c below, in the
  #  graphics app-state group, since it touches GContext internals the same way.)

  "${PEBBLEOS_ROOT}/zephyr-port-apps/watchface_sandboxed/src/port.c"
  "${PEBBLEOS_ROOT}/zephyr-port-apps/watchface_sandboxed/src/sandbox.c"
  "${PEBBLEOS_ROOT}/zephyr-port-apps/watchface_sandboxed/src/sandbox_bridge.c"
  "${PEBBLEOS_ROOT}/zephyr-port-apps/watchface_sandboxed/src/sliding_text_emery_resources.c"
  "${PEBBLEOS_ROOT}/zephyr-port-apps/loader/src/sliding_text_emery_bin.c"
  "${PEBBLEOS_ROOT}/lib/os/mutex_zephyr.c"
  "${PEBBLEOS_ROOT}/lib/os/platform_zephyr.c"
  "${PEBBLEOS_ROOT}/lib/os/semaphore_zephyr.c"
  "${PEBBLEOS_ROOT}/lib/os/tick_zephyr.c"
  "${PEBBLEOS_ROOT}/lib/util/list.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/event_service_client.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/tick_timer_service.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/click.c"
  "${PEBBLEOS_ROOT}/src/fw/services/evented_timer/service.c"
  "${PEBBLEOS_ROOT}/src/fw/kernel/event_loop.c"
  "${PEBBLEOS_ROOT}/src/fw/kernel/events.c"
  "${PEBBLEOS_ROOT}/src/fw/flash_region/filesystem_regions.c"
  "${PEBBLEOS_ROOT}/src/fw/flash_region/flash_region.c"
  "${PEBBLEOS_ROOT}/src/fw/drivers/flash/flash_crc.c"
  "${PEBBLEOS_ROOT}/src/fw/system/firmware_storage.c"
  "${PEBBLEOS_ROOT}/src/fw/kernel/pebble_tasks.c"
  "${PEBBLEOS_ROOT}/src/fw/kernel/task_timer.c"
  "${PEBBLEOS_ROOT}/src/fw/kernel/util/task_init.c"
  "${PEBBLEOS_ROOT}/src/fw/process_management/pebble_process_info.c"
  "${PEBBLEOS_ROOT}/src/fw/services/blob_db/app_db.c"
  "${PEBBLEOS_ROOT}/src/fw/services/event_service/service.c"
  "${PEBBLEOS_ROOT}/src/fw/services/filesystem/flash_translation.c"
  "${PEBBLEOS_ROOT}/src/fw/services/filesystem/pfs.c"
  "${PEBBLEOS_ROOT}/src/fw/services/new_timer/new_timer.c"
  "${PEBBLEOS_ROOT}/src/fw/services/regular_timer/service.c"
  "${PEBBLEOS_ROOT}/src/fw/services/settings/settings_file.c"
  "${PEBBLEOS_ROOT}/src/fw/services/settings/settings_raw_iter.c"
  "${PEBBLEOS_ROOT}/src/fw/services/system_task/service.c"
  "${PEBBLEOS_ROOT}/src/fw/services/tick_timer/service.c"
  "${PEBBLEOS_ROOT}/src/fw/util/crc8.c"
  "${PEBBLEOS_ROOT}/src/fw/util/legacy_checksum.c"

  "${GFX}/8_bit/bitblt_private.c"
  "${GFX}/8_bit/framebuffer.c"
  "${GFX}/arabic_shaping.c"
  "${GFX}/bitblt.c"
  "${GFX}/framebuffer.c"
  "${GFX}/gbitmap.c"
  "${GFX}/gcolor_definitions.c"
  "${GFX}/graphics.c"
  "${GFX}/graphics_bitmap.c"
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
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/menu_layer.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/menu_layer_system_cells.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/scroll_layer.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/inverter_layer.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/animation_timing.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/animation.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/animation_interpolate.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/property_animation.c"
  "${PEBBLEOS_ROOT}/src/fw/services/animation_service/service.c"

  # Real applib UI + graphics pulled in by the Music app (action bar, bitmap /
  # status / progress layers, album-art PNG decode). app_timer + the animation
  # shim layer already exist in the port (input_service.c / ui_shims.c / port.c);
  # only the missing entry points are supplied by the port shim below.
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/action_bar_layer.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/bitmap_layer.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/status_bar_layer.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/progress_layer.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/graphics/gbitmap_png.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/vendor/uPNG/upng.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/vendor/tinflate/tinflate.c"
  src/animation_stubs.c

  "${PEBBLEOS_ROOT}/lib/util/crc32.c"
  "${PEBBLEOS_ROOT}/lib/util/hash.c"
  "${PEBBLEOS_ROOT}/lib/util/keyed_circular_cache.c"
  "${PEBBLEOS_ROOT}/lib/util/iterator.c"
  "${PEBBLEOS_ROOT}/lib/util/math.c"
  "${PEBBLEOS_ROOT}/lib/util/trig.c"
)

set_source_files_properties(
  "${PEBBLEOS_ROOT}/zephyr-port-apps/watchface_sandboxed/src/port.c"
  "${PEBBLEOS_ROOT}/zephyr-port-apps/watchface_sandboxed/src/sandbox_bridge.c"
  PROPERTIES COMPILE_DEFINITIONS "PBL_WATCHFACE_IN_FW=1"
)

set(FW_GRAPHICS_APP_STATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include/sandbox_graphics_state.h"
)
set_source_files_properties(
  "${PEBBLEOS_ROOT}/zephyr-port-apps/watchface_sandboxed/src/port.c"
  "${GFX}/8_bit/bitblt_private.c"
  "${GFX}/8_bit/framebuffer.c"
  "${GFX}/arabic_shaping.c"
  "${GFX}/bitblt.c"
  "${GFX}/framebuffer.c"
  "${GFX}/gbitmap.c"
  "${GFX}/gcolor_definitions.c"
  "${GFX}/graphics.c"
  "${GFX}/graphics_bitmap.c"
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
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/menu_layer.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/menu_layer_system_cells.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/scroll_layer.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/inverter_layer.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/animation_timing.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/bitmap_layer.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/progress_layer.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/graphics/gbitmap_png.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/ui_shims.c"
  PROPERTIES COMPILE_OPTIONS
    "-include;${FW_GRAPHICS_APP_STATE};-iquote;${PEBBLEOS_ROOT}/zephyr-port-apps/watchface_sandboxed/include;-iquote;${PEBBLEOS_ROOT}/zephyr-port-apps/gfx/include;-iquote;${PEBBLEOS_ROOT}/zephyr-port-apps/kernel/include"
)

# port.c now pulls applib animation_private.h (via kernel_applib_state.h)
# alongside Zephyr kernel headers; resolve the sign_extend clash the usual way.
set_source_files_properties(
  "${CMAKE_CURRENT_SOURCE_DIR}/src/port.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/main.c"
  PROPERTIES COMPILE_OPTIONS "-include;fw_zephyr_pre.h"
)

# Real animation engine + service TUs: Zephyr/legacy2/critical-section shims.
set_source_files_properties(
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/animation.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/property_animation.c"
  "${PEBBLEOS_ROOT}/src/fw/services/animation_service/service.c"
  PROPERTIES COMPILE_OPTIONS "-include;anim_port_pre.h"
)

# The shipping QSPI flash sources + the port's qspi_board.c need the SiFli HAL
# umbrella included first (include-order), the Zephyr/Pebble sign_extend fix, the
# board QSPI externs, and the FreeRTOS-style critical-section shim. flash_port_pre.h
# provides all of that; force-include it ahead of each of these translation units.
if(FW_BOARD_PT2)
set_source_files_properties(
  "${PEBBLEOS_ROOT}/src/fw/drivers/flash/gd25q256e.c"
  "${PEBBLEOS_ROOT}/src/fw/drivers/sf32lb52/qspi.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/qspi_board.c"
  PROPERTIES COMPILE_OPTIONS "-include;flash_port_pre.h;-Wno-unused-function"
)

# SiFli HAL sources: quiet the vendor code's warnings so it builds under the
# app's stricter flags.
set_source_files_properties(
  "${SIFLI_SDK}/drivers/hal/bf0_hal_mpi.c"
  "${SIFLI_SDK}/drivers/hal/bf0_hal_mpi_ex.c"
  "${SIFLI_SDK}/drivers/hal/bf0_hal_dma.c"
  "${SIFLI_SDK}/drivers/hal/flash_table.c"
  PROPERTIES COMPILE_OPTIONS "-Wno-unused-variable;-Wno-unused-function;-Wno-sign-compare;-Wno-pointer-arith;-Wno-old-style-declaration"
)
endif()

zephyr_linker_sources(ROM_SECTIONS
  "${PEBBLEOS_ROOT}/zephyr-port-apps/watchface_sandboxed/src/sandbox_syscalls.ld"
)

generate_inc_file_for_target(app
  "${PEBBLEOS_ROOT}/resources/common/base/pbf/GOTHIC_14.pbf"
  "${CMAKE_BINARY_DIR}/include/generated/watchface_font_small.pbf.inc"
)

# Real system fonts used by the launcher menu + Settings/Music/Alarms cells,
# embedded as .inc blobs and served by watchface_sandboxed/src/port.c (gated on
# PBL_WATCHFACE_IN_FW). fonts_get_system_font() maps FONT_KEY_* -> these faces.
generate_inc_file_for_target(app
  "${PEBBLEOS_ROOT}/resources/normal/base/pbf/GOTHIC_14_BOLD.pbf"
  "${CMAKE_BINARY_DIR}/include/generated/fw_gothic_14_bold.pbf.inc"
)
generate_inc_file_for_target(app
  "${PEBBLEOS_ROOT}/resources/normal/base/pbf/GOTHIC_18.pbf"
  "${CMAKE_BINARY_DIR}/include/generated/fw_gothic_18.pbf.inc"
)
generate_inc_file_for_target(app
  "${PEBBLEOS_ROOT}/resources/common/base/pbf/GOTHIC_18_BOLD.pbf"
  "${CMAKE_BINARY_DIR}/include/generated/fw_gothic_18_bold.pbf.inc"
)
generate_inc_file_for_target(app
  "${PEBBLEOS_ROOT}/resources/normal/base/pbf/GOTHIC_24_BOLD.pbf"
  "${CMAKE_BINARY_DIR}/include/generated/fw_gothic_24_bold.pbf.inc"
)
generate_inc_file_for_target(app
  "${PEBBLEOS_ROOT}/resources/normal/base/pbf/GOTHIC_28.pbf"
  "${CMAKE_BINARY_DIR}/include/generated/fw_gothic_28.pbf.inc"
)
generate_inc_file_for_target(app
  "${PEBBLEOS_ROOT}/resources/normal/base/pbf/GOTHIC_28_BOLD.pbf"
  "${CMAKE_BINARY_DIR}/include/generated/fw_gothic_28_bold.pbf.inc"
)
generate_inc_file_for_target(app
  "${PEBBLEOS_ROOT}/resources/common/base/pbf/GOTHIC_24.pbf"
  "${CMAKE_BINARY_DIR}/include/generated/fw_gothic_24.pbf.inc"
)

# Real Music action-bar icons: the firmware build converts these PNGs to pbi8,
# but gbitmap_init_with_resource() auto-detects the embedded PNG bytes, so embed
# the source PNGs directly.
foreach(icon play pause fast_forward rewind skip_forward skip_backward volume_up volume_down ellipsis)
  generate_inc_file_for_target(app
    "${PEBBLEOS_ROOT}/resources/normal/base/images/music_icon_${icon}.png"
    "${CMAKE_BINARY_DIR}/include/generated/fw_music_icon_${icon}.png.inc"
  )
endforeach()

# Launcher per-row app icons + status-bar BT glyph, embedded as PNG blobs and
# served through the same sys_resource_* path as the fonts/music icons (see
# watchface_sandboxed/src/port.c). Real launcher-glance PNGs are used where they
# exist (notifications/watchfaces/generic); the Settings/Music/Alarms launcher
# icons ship as vector PDC in the resource pack, so their 25x25 source SVGs were
# rasterised to PNG under assets/launcher_icons/ (no PDC renderer in the port).
generate_inc_file_for_target(app
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/launcher_icons/settings_tiny.png"
  "${CMAKE_BINARY_DIR}/include/generated/fw_icon_settings.png.inc"
)
generate_inc_file_for_target(app
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/launcher_icons/audio_cassette_tiny.png"
  "${CMAKE_BINARY_DIR}/include/generated/fw_icon_music.png.inc"
)
generate_inc_file_for_target(app
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/launcher_icons/alarm_clock_tiny.png"
  "${CMAKE_BINARY_DIR}/include/generated/fw_icon_alarms.png.inc"
)
generate_inc_file_for_target(app
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/launcher_icons/notifications.png"
  "${CMAKE_BINARY_DIR}/include/generated/fw_icon_notifications.png.inc"
)
generate_inc_file_for_target(app
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/launcher_icons/watchfaces.png"
  "${CMAKE_BINARY_DIR}/include/generated/fw_icon_watchfaces.png.inc"
)
generate_inc_file_for_target(app
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/launcher_icons/generic.png"
  "${CMAKE_BINARY_DIR}/include/generated/fw_icon_generic.png.inc"
)
generate_inc_file_for_target(app
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/launcher_icons/bt_disconnected.png"
  "${CMAKE_BINARY_DIR}/include/generated/fw_icon_bt_disconnected.png.inc"
)

set_source_files_properties(
  "${PEBBLEOS_ROOT}/src/fw/kernel/task_timer.c"
  "${PEBBLEOS_ROOT}/src/fw/services/regular_timer/service.c"
  PROPERTIES COMPILE_OPTIONS "-include;pbl/drivers/rtc.h"
)

set_source_files_properties(
  "${PEBBLEOS_ROOT}/src/fw/services/blob_db/app_db.c"
  PROPERTIES COMPILE_OPTIONS "-include;string.h"
)

set_source_files_properties(
  "${PEBBLEOS_ROOT}/src/fw/services/settings/settings_file.c"
  PROPERTIES COMPILE_DEFINITIONS "SECONDS_PER_DAY=86400"
)

set_source_files_properties(
  "${PEBBLEOS_ROOT}/src/fw/services/settings/settings_raw_iter.c"
  PROPERTIES COMPILE_OPTIONS "-Wno-unused-variable"
)

set_source_files_properties(
  "${PEBBLEOS_ROOT}/src/fw/services/system_task/service.c"
  PROPERTIES COMPILE_OPTIONS "-include;zephyr/kernel.h;-include;system/reset.h"
)

# tictoc.c references RESOURCE_ID_MENU_ICON_TICTOC_WATCH only for its md's
# launcher icon; the port has no generated resource IDs, so define it to 0 (the
# launcher menu draws names, not icons). fw_zephyr_pre.h resolves the Zephyr vs
# Pebble sign_extend() collision (both pulled by real app sources).
# On pt2 (no real resource pack) the generated resource IDs don't exist, so the
# md-icon / app-icon IDs are defined to 0; qemu uses the real generated IDs.
if(FW_BOARD_PT2)
  set(FW_TICTOC_RES_DEFS "RESOURCE_ID_MENU_ICON_TICTOC_WATCH=0")
  set(FW_MUSIC_RES_DEFS "RESOURCE_ID_AUDIO_CASSETTE_TINY=0;RESOURCE_ID_MUSIC_ICON_ELLIPSIS=278;RESOURCE_ID_MUSIC_ICON_FAST_FORWARD=272;RESOURCE_ID_MUSIC_ICON_PAUSE=271;RESOURCE_ID_MUSIC_ICON_PLAY=270;RESOURCE_ID_MUSIC_ICON_PLAY_PAUSE=0;RESOURCE_ID_MUSIC_ICON_REWIND=273;RESOURCE_ID_MUSIC_ICON_SKIP_BACKWARD=275;RESOURCE_ID_MUSIC_ICON_SKIP_FORWARD=274;RESOURCE_ID_MUSIC_ICON_VOLUME_DOWN=277;RESOURCE_ID_MUSIC_ICON_VOLUME_UP=276;RESOURCE_ID_MUSIC_IMAGE_NO_MUSIC=0;RESOURCE_ID_MUSIC_LARGE_CASSETTE=0;RESOURCE_ID_MUSIC_LARGE_PAUSED=0;RESOURCE_ID_MUSIC_LARGE_VOLUME_DOWN=0;RESOURCE_ID_MUSIC_LARGE_VOLUME_UP=0;RESOURCE_ID_VIBE_SCORE_HAPTIC_FEEDBACK=0;")
  set(FW_ALARMS_RES_DEFS "RESOURCE_ID_ACTION_BAR_ICON_CHECK=0;RESOURCE_ID_ALARM_CLOCK_TINY=0;RESOURCE_ID_GENERIC_CONFIRMATION_LARGE=0;RESOURCE_ID_PLUS_ICON_BLACK=0;RESOURCE_ID_PLUS_ICON_DOTTED=0;RESOURCE_ID_RESULT_SHREDDED_LARGE=0;RESOURCE_ID_SMART_ALARM_ICON_BLACK=0;RESOURCE_ID_SMART_ALARM_TINY=0")
  set(FW_WATCHFACES_RES_DEFS "RESOURCE_ID_WATCHFACES_APP_GLANCE=0;RESOURCE_ID_MENU_LAYER_GENERIC_WATCHFACE_ICON=0")
  set(FW_SETTINGS_RES_DEFS "RESOURCE_ID_SETTINGS_TINY=0")
else()
  set(FW_TICTOC_RES_DEFS "")
  set(FW_MUSIC_RES_DEFS "")
  set(FW_ALARMS_RES_DEFS "")
  set(FW_WATCHFACES_RES_DEFS "")
  set(FW_SETTINGS_RES_DEFS "")
endif()

set_source_files_properties(
  "${PEBBLEOS_ROOT}/src/fw/apps/watch/tictoc/tictoc.c"
  PROPERTIES
    COMPILE_DEFINITIONS "${FW_TICTOC_RES_DEFS}"
    COMPILE_OPTIONS "-include;fw_zephyr_pre.h"
)

# app_service_stubs.c pulls Pebble applib headers (fonts/window) that trip the
# Zephyr vs Pebble sign_extend collision; fw_zephyr_pre.h resolves it and keeps
# fw/include's font_resource_keys.auto.h shadow ahead of the sandbox copy.
set_source_files_properties(
  "${CMAKE_CURRENT_SOURCE_DIR}/src/app_service_stubs.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/animation_stubs.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/app_alarm_stubs.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/app_ui_window_stubs.c"
  PROPERTIES COMPILE_OPTIONS "-include;fw_zephyr_pre.h"
)

# action_bar_layer.c and status_bar_layer.c need the graphics-app-state treatment
# AND the port's fw/include shadows (process_manager current-platform, time_ms,
# clock_copy_time_string, app_state_get_window_stack, and status bar's extra font
# keys) which the sandbox/gfx/kernel -iquote roots don't carry — so their -iquote
# list puts fw/include first. Neither uses APP_LOG, so the slim fw/include
# app_logging shadow is harmless here. status_bar also needs TIMER_INVALID_ID
# (new_timer sentinel, == 0) for its AppTimer field.
# These two TUs reference process_manager_current_platform / PBL_PLATFORM_SWITCH /
# PlatformType (action bar, status bar) and time_ms (action bar) that shipping
# pulls in transitively; force-include the port's process_manager shadow (which
# pulls applib/platform.h) and time shadow so they resolve without chasing the
# transitive header chain.
set(FW_UI_WITH_PORT_SHADOWS_OPTS
  "-include;${FW_GRAPHICS_APP_STATE};-include;process_management/process_manager.h;-include;util/time/time.h;-iquote;${CMAKE_CURRENT_SOURCE_DIR}/include;-iquote;${PEBBLEOS_ROOT}/zephyr-port-apps/watchface_sandboxed/include;-iquote;${PEBBLEOS_ROOT}/zephyr-port-apps/gfx/include;-iquote;${PEBBLEOS_ROOT}/zephyr-port-apps/kernel/include"
)
set_source_files_properties(
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/action_bar_layer.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/menu_layer_system_cells.c"
  PROPERTIES COMPILE_OPTIONS "${FW_UI_WITH_PORT_SHADOWS_OPTS}"
)
set_source_files_properties(
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/status_bar_layer.c"
  PROPERTIES
    COMPILE_DEFINITIONS "TIMER_INVALID_ID=0"
    COMPILE_OPTIONS "${FW_UI_WITH_PORT_SHADOWS_OPTS}"
)

# music.c: same sign_extend pre-include as tictoc, plus its launcher/action-bar
# icon + vibe resource IDs defined to 0 (the port has no generated resource IDs;
# icons are cosmetic — see SYSTEM-APPS-BUILDOUT.md).
set_source_files_properties(
  "${PEBBLEOS_ROOT}/src/fw/apps/system/music.c"
  PROPERTIES
    COMPILE_OPTIONS "-include;fw_zephyr_pre.h"
    COMPILE_DEFINITIONS "${FW_MUSIC_RES_DEFS}SECONDS_PER_HOUR=3600;SECONDS_PER_MINUTE=60"
)

# Alarms app trio: same sign_extend pre-include as music, with its launcher /
# menu / dialog icon resource IDs defined to 0 (cosmetic; the port has no
# generated resource IDs).
set_source_files_properties(
  "${PEBBLEOS_ROOT}/src/fw/apps/system/alarms/alarms.c"
  "${PEBBLEOS_ROOT}/src/fw/apps/system/alarms/alarm_editor.c"
  "${PEBBLEOS_ROOT}/src/fw/apps/system/alarms/alarm_detail.c"
  PROPERTIES
    COMPILE_OPTIONS "-include;fw_zephyr_pre.h;-include;applib/app_launch_reason.h"
    COMPILE_DEFINITIONS "${FW_ALARMS_RES_DEFS}"
)

# tictoc_default.c calls rtc_get_time_tm without including a header for it (in
# shipping it rode an implicit declaration); force-include the port rtc header,
# after fw_zephyr_pre.h.
set_source_files_properties(
  "${PEBBLEOS_ROOT}/src/fw/apps/watch/tictoc/default/tictoc_default.c"
  PROPERTIES COMPILE_OPTIONS "-include;fw_zephyr_pre.h;-include;pbl/drivers/rtc.h"
)

# Watchfaces + Settings real app sources and the port glue pull both Zephyr (via
# FreeRTOS.h) and Pebble headers, so they need fw_zephyr_pre.h for the
# sign_extend collision. They also include "process_state/app_state/app_state.h"
# and expect the PORT app_state (app_state_get_user_data etc.), so -iquote the
# watchface_sandboxed include dir ahead of the real fw one.
set(FW_APP_IQUOTE "${PEBBLEOS_ROOT}/zephyr-port-apps/watchface_sandboxed/include")
set_source_files_properties(
  "${CMAKE_CURRENT_SOURCE_DIR}/src/apps_port_glue.c"
  "${PEBBLEOS_ROOT}/src/fw/apps/system/watchfaces.c"
  "${PEBBLEOS_ROOT}/src/fw/apps/system/settings/settings.c"
  "${PEBBLEOS_ROOT}/src/fw/apps/system/settings/menu.c"
  "${PEBBLEOS_ROOT}/src/fw/apps/system/settings/window.c"
  "${PEBBLEOS_ROOT}/src/fw/apps/system/settings/health.c"
  PROPERTIES COMPILE_OPTIONS "-include;fw_zephyr_pre.h;-iquote;${FW_APP_IQUOTE}"
)

# The port has no generated resource IDs; the launcher menu + settings menu draw
# names, not icons. Define the launcher-icon IDs these mds reference to 0.
set_source_files_properties(
  "${PEBBLEOS_ROOT}/src/fw/apps/system/watchfaces.c"
  PROPERTIES COMPILE_DEFINITIONS "${FW_WATCHFACES_RES_DEFS}"
)
set_source_files_properties(
  "${PEBBLEOS_ROOT}/src/fw/apps/system/settings/settings.c"
  PROPERTIES COMPILE_DEFINITIONS "${FW_SETTINGS_RES_DEFS}"
)

# The port has no unobstructed-area service and runs system apps on the KernelMain
# UI loop, not a separate PebbleTask_App task, so applib/ui/layer.c's
# layer_get_unobstructed_bounds* (which assert PebbleTask_App and pull the
# unobstructed_area_service) are renamed out of this translation unit and replaced
# by full-bounds shims in src/system_app.c. See SYSTEM-APPS-BUILDOUT.md.
set_source_files_properties(
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/layer.c"
  PROPERTIES COMPILE_DEFINITIONS "layer_get_unobstructed_bounds=port_layer_gub_unused;layer_get_unobstructed_bounds_by_value=port_layer_gubv_unused"
)

# ---------------------------------------------------------------------------
# Real shell (qemu_emery): TicToc at boot + the REAL launcher app with its
# glance service, kino reel PDC icons, and the real system resource pack read
# from the XIP-mapped SPI flash image.
# ---------------------------------------------------------------------------
if(NOT FW_BOARD_PT2)
set(LAUNCHER "${PEBBLEOS_ROOT}/src/fw/apps/system/launcher/default")
set(KINO "${PEBBLEOS_ROOT}/src/fw/applib/ui/kino")

target_sources(app PRIVATE
  src/fw_shell.c
  src/boot_splash_port.c
  src/shell_glue.c
  src/resource_pack.c

  # Real settings submodules (qemu shell only; pt2 keeps the apps_port_glue.c
  # stub rows until its service closures are ported). TU properties are set
  # after this block (extra -include for headers shipping pulls via
  # kernel/events.h; sniprintf is newlib-only, the minimal libc spells it
  # snprintf).
  "${PEBBLEOS_ROOT}/src/fw/apps/system/settings/bluetooth.c"
  "${PEBBLEOS_ROOT}/src/fw/apps/system/settings/display.c"
  "${PEBBLEOS_ROOT}/src/fw/apps/system/settings/time.c"
  "${PEBBLEOS_ROOT}/src/fw/apps/system/settings/option_menu.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/option_menu_window.c"
  "${PEBBLEOS_ROOT}/src/fw/services/timezone_database/service.c"

  "${LAUNCHER}/launcher.c"
  "${LAUNCHER}/menu_layer.c"
  "${LAUNCHER}/app_glance.c"
  "${LAUNCHER}/app_glance_private.c"
  "${LAUNCHER}/app_glance_service.c"
  "${LAUNCHER}/app_glance_structured.c"
  "${LAUNCHER}/app_glance_generic.c"
  "${LAUNCHER}/app_glance_settings.c"
  "${LAUNCHER}/app_glance_music.c"
  "${LAUNCHER}/app_glance_notifications.c"
  "${LAUNCHER}/app_glance_alarms.c"
  "${LAUNCHER}/app_glance_watchfaces.c"
  "${LAUNCHER}/app_glance_weather.c"
  "${LAUNCHER}/app_glance_workout.c"

  "${KINO}/kino_reel.c"
  "${KINO}/kino_reel_custom.c"
  "${KINO}/kino_player.c"
  "${KINO}/kino_reel_gbitmap.c"
  "${KINO}/kino_reel_gbitmap_sequence.c"
  "${KINO}/kino_reel_pdci.c"
  "${KINO}/kino_reel_pdcs.c"

  "${GFX}/gdraw_command.c"
  "${GFX}/gdraw_command_frame.c"
  "${GFX}/gdraw_command_image.c"
  "${GFX}/gdraw_command_list.c"
  "${GFX}/gdraw_command_sequence.c"
  "${GFX}/gdraw_command_transforms.c"
  "${GFX}/gpath.c"
  "${GFX}/gbitmap_sequence.c"

  "${PEBBLEOS_ROOT}/src/fw/apps/system/timeline/text_node.c"
  "${PEBBLEOS_ROOT}/src/fw/services/timeline/timeline_resources.c"
  "${PEBBLEOS_ROOT}/build/src/fw/resource/timeline_resource_table.auto.c"
  "${PEBBLEOS_ROOT}/lib/util/uuid.c"

  # Real compositor transitions (watchface <-> launcher shutter) on the port
  # compositor core (src/compositor_port.c).
  src/compositor_port.c
  "${PEBBLEOS_ROOT}/src/fw/services/compositor/compositor_transitions.c"
  "${PEBBLEOS_ROOT}/src/fw/services/compositor/default/compositor_shutter_transitions.c"
  "${PEBBLEOS_ROOT}/src/fw/services/compositor/default/compositor_launcher_app_transitions.c"
)

set_source_files_properties(
  "${PEBBLEOS_ROOT}/src/fw/apps/system/settings/bluetooth.c"
  PROPERTIES
    COMPILE_OPTIONS "-include;fw_zephyr_pre.h;-include;pbl/services/bluetooth/bluetooth_ctl.h;-iquote;${FW_APP_IQUOTE}"
    COMPILE_DEFINITIONS "sniprintf=snprintf"
)
# display.c row set must match the reference build's (touch + app-scaling rows).
set_source_files_properties(
  "${PEBBLEOS_ROOT}/src/fw/apps/system/settings/display.c"
  PROPERTIES
    COMPILE_OPTIONS "-include;fw_zephyr_pre.h;-iquote;${FW_APP_IQUOTE}"
    COMPILE_DEFINITIONS "CONFIG_TOUCH=1;CONFIG_APP_SCALING=1"
)
set_source_files_properties(
  "${PEBBLEOS_ROOT}/src/fw/apps/system/settings/option_menu.c"
  "${PEBBLEOS_ROOT}/src/fw/apps/system/settings/time.c"
  PROPERTIES COMPILE_OPTIONS "-include;fw_zephyr_pre.h;-iquote;${FW_APP_IQUOTE}"
)
# Same shadow set as the other applib UI TUs, but with the real generated
# resource IDs (radio-button icons come from the system pack) resolved ahead of
# the gfx shadow header.
set_source_files_properties(
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/option_menu_window.c"
  PROPERTIES COMPILE_OPTIONS
    "-include;${FW_GRAPHICS_APP_STATE};-include;process_management/process_manager.h;-include;util/time/time.h;-iquote;${CMAKE_CURRENT_SOURCE_DIR}/include;-iquote;${PEBBLEOS_ROOT}/build/src/fw;-iquote;${PEBBLEOS_ROOT}/zephyr-port-apps/watchface_sandboxed/include;-iquote;${PEBBLEOS_ROOT}/zephyr-port-apps/gfx/include;-iquote;${PEBBLEOS_ROOT}/zephyr-port-apps/kernel/include"
)

# Launcher app TUs: same treatment as the settings/watchfaces app sources, plus
# the port shadow declarations they pull implicitly in shipping.
set_source_files_properties(
  "${LAUNCHER}/launcher.c"
  "${LAUNCHER}/menu_layer.c"
  "${LAUNCHER}/app_glance.c"
  "${LAUNCHER}/app_glance_private.c"
  "${LAUNCHER}/app_glance_service.c"
  "${LAUNCHER}/app_glance_structured.c"
  "${LAUNCHER}/app_glance_generic.c"
  "${LAUNCHER}/app_glance_settings.c"
  "${LAUNCHER}/app_glance_music.c"
  "${LAUNCHER}/app_glance_notifications.c"
  "${LAUNCHER}/app_glance_alarms.c"
  "${LAUNCHER}/app_glance_watchfaces.c"
  "${LAUNCHER}/app_glance_weather.c"
  "${LAUNCHER}/app_glance_workout.c"
  PROPERTIES COMPILE_OPTIONS
    "-include;fw_zephyr_pre.h;-include;applib/ui/window.h;-include;pbl/services/timeline/item.h;-include;pbl/services/bluetooth/bluetooth_ctl.h;-include;process_management/app_manager.h;-include;process_management/process_manager.h;-include;pbl/drivers/rtc.h;-iquote;${FW_APP_IQUOTE}"
)

# Graphics-flavored TUs (kino reels, PDC drawing, text nodes) get the same
# app-state pre-include as the rest of the applib graphics group.
set_source_files_properties(
  "${KINO}/kino_reel.c"
  "${KINO}/kino_reel_custom.c"
  "${KINO}/kino_player.c"
  "${KINO}/kino_reel_gbitmap.c"
  "${KINO}/kino_reel_gbitmap_sequence.c"
  "${KINO}/kino_reel_pdci.c"
  "${KINO}/kino_reel_pdcs.c"
  "${GFX}/gdraw_command.c"
  "${GFX}/gdraw_command_frame.c"
  "${GFX}/gdraw_command_image.c"
  "${GFX}/gdraw_command_list.c"
  "${GFX}/gdraw_command_sequence.c"
  "${GFX}/gdraw_command_transforms.c"
  "${GFX}/gpath.c"
  "${GFX}/gbitmap_sequence.c"
  "${PEBBLEOS_ROOT}/src/fw/apps/system/timeline/text_node.c"
  "${PEBBLEOS_ROOT}/src/fw/services/timeline/timeline_resources.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/compositor_port.c"
  "${PEBBLEOS_ROOT}/src/fw/services/compositor/compositor_transitions.c"
  "${PEBBLEOS_ROOT}/src/fw/services/compositor/default/compositor_shutter_transitions.c"
  "${PEBBLEOS_ROOT}/src/fw/services/compositor/default/compositor_launcher_app_transitions.c"
  PROPERTIES COMPILE_OPTIONS
    "-include;${FW_GRAPHICS_APP_STATE};-include;resource/resource_ids.auto.h;-include;applib/app_logging.h;-iquote;${PEBBLEOS_ROOT}/build/src/fw;-iquote;${CMAKE_CURRENT_SOURCE_DIR}/include;-iquote;${PEBBLEOS_ROOT}/zephyr-port-apps/watchface_sandboxed/include;-iquote;${PEBBLEOS_ROOT}/zephyr-port-apps/gfx/include;-iquote;${PEBBLEOS_ROOT}/zephyr-port-apps/kernel/include"
)

# port.c keeps serving its embedded resources (fonts, music icons, FW_RES_*)
# but under port_* names; resource_pack.c owns the real symbols and adds the
# system pbpack from flash (see resource_pack.c).
set_property(SOURCE "${PEBBLEOS_ROOT}/zephyr-port-apps/watchface_sandboxed/src/port.c"
  APPEND PROPERTY COMPILE_DEFINITIONS
  sys_resource_is_valid=port_sys_resource_is_valid
  sys_resource_size=port_sys_resource_size
  sys_resource_load_range=port_sys_resource_load_range
  sys_resource_get_and_cache=port_sys_resource_get_and_cache
  sys_resource_read_only_bytes=port_sys_resource_read_only_bytes
  sys_resource_bytes_are_readonly=port_sys_resource_bytes_are_readonly
  applib_resource_mmap_or_load=port_applib_resource_mmap_or_load
  applib_resource_munmap_or_free=port_applib_resource_munmap_or_free
  applib_resource_get_handle=port_applib_resource_get_handle
)
endif()
