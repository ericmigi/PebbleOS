# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(USER_CACHE_DIR "${CMAKE_BINARY_DIR}/.cache" CACHE PATH "Zephyr cache directory")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pebbleos_zephyr_pfs)

get_filename_component(PEBBLEOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

target_include_directories(app PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${PEBBLEOS_ROOT}/include"
  "${PEBBLEOS_ROOT}/src/fw"
)

target_compile_definitions(app PRIVATE
  CONFIG_FLASH_GD25Q256E=1
  CONFIG_PBLBOOT=1
  CONFIG_RECOVERY_FW=1
  CONFIG_SERVICE_FILESYSTEM_LOG_LEVEL=0
  CONFIG_SOC_SF32LB52=1
)

target_sources(app PRIVATE
  src/flash_shim.c
  src/main.c
  src/port.c
  "${PEBBLEOS_ROOT}/lib/os/mutex_zephyr.c"
  "${PEBBLEOS_ROOT}/lib/os/platform_zephyr.c"
  "${PEBBLEOS_ROOT}/lib/os/semaphore_zephyr.c"
  "${PEBBLEOS_ROOT}/lib/os/tick_zephyr.c"
  "${PEBBLEOS_ROOT}/lib/util/list.c"
  "${PEBBLEOS_ROOT}/src/fw/flash_region/filesystem_regions.c"
  "${PEBBLEOS_ROOT}/src/fw/flash_region/flash_region.c"
  "${PEBBLEOS_ROOT}/src/fw/services/filesystem/flash_translation.c"
  "${PEBBLEOS_ROOT}/src/fw/services/filesystem/pfs.c"
  "${PEBBLEOS_ROOT}/src/fw/util/crc8.c"
  "${PEBBLEOS_ROOT}/src/fw/util/legacy_checksum.c"
)
