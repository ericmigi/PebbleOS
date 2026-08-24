# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(USER_CACHE_DIR "${CMAKE_BINARY_DIR}/.cache" CACHE PATH "Zephyr cache directory")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pebbleos_zephyr_fw)

get_filename_component(PEBBLEOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
string(TIMESTAMP FW_BUILD_EPOCH "%s" UTC)

target_include_directories(app PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${PEBBLEOS_ROOT}/include"
  "${PEBBLEOS_ROOT}/src/fw"
)

target_compile_definitions(app PRIVATE
  CONFIG_FLASH_GD25Q256E=1
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
)

target_sources(app PRIVATE
  src/app_registry.c
  src/app_registry_stubs.c
  src/flash_shim.c
  src/freertos_compat.c
  src/main.c
  src/pfs_boot.c
  src/port.c
  src/wall_clock_zephyr.c
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
