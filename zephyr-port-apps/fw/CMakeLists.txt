# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(USER_CACHE_DIR "${CMAKE_BINARY_DIR}/.cache" CACHE PATH "Zephyr cache directory")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pebbleos_zephyr_fw)

get_filename_component(PEBBLEOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
string(TIMESTAMP FW_BUILD_EPOCH "%s" UTC)
set(GFX "${PEBBLEOS_ROOT}/src/fw/applib/graphics")

target_include_directories(app PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${PEBBLEOS_ROOT}/zephyr-port-apps/watchface_sandboxed/include"
  "${PEBBLEOS_ROOT}/zephyr-port-apps/loader/src"
  "${PEBBLEOS_ROOT}/include"
  "${PEBBLEOS_ROOT}/src/fw"
  "${PEBBLEOS_ROOT}/src/fw/applib/vendor/uPNG"
  "${PEBBLEOS_ROOT}/zephyr-port-apps/kernel/include"
  "${PEBBLEOS_ROOT}/zephyr-port-apps/gfx/include"
  "${CMAKE_BINARY_DIR}/include/generated"
)

target_compile_definitions(app PRIVATE
  CONFIG_FLASH_GD25Q256E=1
  CONFIG_BOARD_OBELIX_PVT=1
  CONFIG_PLATFORM_EMERY=1
  CONFIG_SCREEN_COLOR_DEPTH_BITS=8
  CONFIG_PBLBOOT=1
  CONFIG_SOC_SF32LB52=1
  CONFIG_PEBBLE_ZEPHYR_CORE_BOOT=1
  CONFIG_RECOVERY_FW=1
  CONFIG_RELEASE=1
  CONFIG_TASK_TIMER_POOL_SIZE=16
  CONFIG_SERVICE_EVENT_SERVICE_LOG_LEVEL=0
  CONFIG_SERVICE_FILESYSTEM_LOG_LEVEL=0
  CONFIG_SERVICE_NEW_TIMER_LOG_LEVEL=0
  CONFIG_SERVICE_REGULAR_TIMER_LOG_LEVEL=0
  CONFIG_SERVICE_SYSTEM_TASK_LOG_LEVEL=0
  CONFIG_SERVICE_TICK_TIMER_LOG_LEVEL=0
  FW_BUILD_EPOCH=${FW_BUILD_EPOCH}
  PBL_WTF_CONTEXT=1
)

target_compile_options(app PRIVATE
  -include string.h
  -Wno-address-of-packed-member
)

target_sources(app PRIVATE
  src/app_registry.c
  src/app_registry_stubs.c
  src/flash_shim.c
  src/freertos_compat.c
  src/main.c
  src/pfs_boot.c
  src/port.c
  src/sandbox_launcher.c
  src/wall_clock_zephyr.c
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
  "${PEBBLEOS_ROOT}/src/fw/kernel/event_loop.c"
  "${PEBBLEOS_ROOT}/src/fw/kernel/events.c"
  "${PEBBLEOS_ROOT}/src/fw/flash_region/filesystem_regions.c"
  "${PEBBLEOS_ROOT}/src/fw/flash_region/flash_region.c"
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
  PROPERTIES COMPILE_OPTIONS
    "-include;${FW_GRAPHICS_APP_STATE};-iquote;${PEBBLEOS_ROOT}/zephyr-port-apps/watchface_sandboxed/include;-iquote;${PEBBLEOS_ROOT}/zephyr-port-apps/gfx/include;-iquote;${PEBBLEOS_ROOT}/zephyr-port-apps/kernel/include"
)

zephyr_linker_sources(ROM_SECTIONS
  "${PEBBLEOS_ROOT}/zephyr-port-apps/watchface_sandboxed/src/sandbox_syscalls.ld"
)

generate_inc_file_for_target(app
  "${PEBBLEOS_ROOT}/resources/common/base/pbf/GOTHIC_14.pbf"
  "${CMAKE_BINARY_DIR}/include/generated/watchface_font_small.pbf.inc"
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
