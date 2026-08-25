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
  "${PEBBLEOS_ROOT}/src/fw/applib/vendor/tinflate"
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

target_compile_options(app PRIVATE
  -include string.h
  -Wno-address-of-packed-member
)

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
  src/flash_shim.c
  src/freertos_compat.c
  src/main.c
  src/pfs_boot.c
  src/port.c
  src/sandbox_launcher.c
  src/system_app.c
  src/wall_clock_zephyr.c

  "${PEBBLEOS_ROOT}/src/fw/board/boards/board_obelix_zephyr.c"
  "${PEBBLEOS_ROOT}/src/fw/drivers/task_watchdog_zephyr.c"
  "${PEBBLEOS_ROOT}/src/fw/drivers/watchdog/zephyr.c"
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
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/menu_layer.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/menu_layer_system_cells.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/scroll_layer.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/inverter_layer.c"
  "${PEBBLEOS_ROOT}/src/fw/applib/ui/animation_timing.c"

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

# Real Music action-bar icons: the firmware build converts these PNGs to pbi8,
# but gbitmap_init_with_resource() auto-detects the embedded PNG bytes, so embed
# the source PNGs directly.
foreach(icon play pause fast_forward rewind skip_forward skip_backward volume_up volume_down ellipsis)
  generate_inc_file_for_target(app
    "${PEBBLEOS_ROOT}/resources/normal/base/images/music_icon_${icon}.png"
    "${CMAKE_BINARY_DIR}/include/generated/fw_music_icon_${icon}.png.inc"
  )
endforeach()

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
set_source_files_properties(
  "${PEBBLEOS_ROOT}/src/fw/apps/watch/tictoc/tictoc.c"
  PROPERTIES
    COMPILE_DEFINITIONS "RESOURCE_ID_MENU_ICON_TICTOC_WATCH=0"
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
    COMPILE_DEFINITIONS "RESOURCE_ID_AUDIO_CASSETTE_TINY=0;RESOURCE_ID_MUSIC_ICON_ELLIPSIS=278;RESOURCE_ID_MUSIC_ICON_FAST_FORWARD=272;RESOURCE_ID_MUSIC_ICON_PAUSE=271;RESOURCE_ID_MUSIC_ICON_PLAY=270;RESOURCE_ID_MUSIC_ICON_PLAY_PAUSE=0;RESOURCE_ID_MUSIC_ICON_REWIND=273;RESOURCE_ID_MUSIC_ICON_SKIP_BACKWARD=275;RESOURCE_ID_MUSIC_ICON_SKIP_FORWARD=274;RESOURCE_ID_MUSIC_ICON_VOLUME_DOWN=277;RESOURCE_ID_MUSIC_ICON_VOLUME_UP=276;RESOURCE_ID_MUSIC_IMAGE_NO_MUSIC=0;RESOURCE_ID_MUSIC_LARGE_CASSETTE=0;RESOURCE_ID_MUSIC_LARGE_PAUSED=0;RESOURCE_ID_MUSIC_LARGE_VOLUME_DOWN=0;RESOURCE_ID_MUSIC_LARGE_VOLUME_UP=0;RESOURCE_ID_VIBE_SCORE_HAPTIC_FEEDBACK=0;SECONDS_PER_HOUR=3600;SECONDS_PER_MINUTE=60"
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
    COMPILE_DEFINITIONS "RESOURCE_ID_ACTION_BAR_ICON_CHECK=0;RESOURCE_ID_ALARM_CLOCK_TINY=0;RESOURCE_ID_GENERIC_CONFIRMATION_LARGE=0;RESOURCE_ID_PLUS_ICON_BLACK=0;RESOURCE_ID_PLUS_ICON_DOTTED=0;RESOURCE_ID_RESULT_SHREDDED_LARGE=0;RESOURCE_ID_SMART_ALARM_ICON_BLACK=0;RESOURCE_ID_SMART_ALARM_TINY=0"
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
  PROPERTIES COMPILE_DEFINITIONS
    "RESOURCE_ID_WATCHFACES_APP_GLANCE=0;RESOURCE_ID_MENU_LAYER_GENERIC_WATCHFACE_ICON=0"
)
set_source_files_properties(
  "${PEBBLEOS_ROOT}/src/fw/apps/system/settings/settings.c"
  PROPERTIES COMPILE_DEFINITIONS "RESOURCE_ID_SETTINGS_TINY=0"
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
