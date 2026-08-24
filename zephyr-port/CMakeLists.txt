# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(USER_CACHE_DIR "${CMAKE_BINARY_DIR}/.cache" CACHE PATH "Zephyr cache directory")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(pebbleos_zephyr_smoke)

get_filename_component(PEBBLEOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

target_include_directories(app PRIVATE "${PEBBLEOS_ROOT}/include")

target_sources(app PRIVATE
  src/main.c
  "${PEBBLEOS_ROOT}/lib/os/mutex_zephyr.c"
  "${PEBBLEOS_ROOT}/lib/os/platform_zephyr.c"
  "${PEBBLEOS_ROOT}/lib/os/semaphore_zephyr.c"
  "${PEBBLEOS_ROOT}/lib/os/tick_zephyr.c"
  "${PEBBLEOS_ROOT}/lib/util/circular_buffer.c"
  "${PEBBLEOS_ROOT}/lib/util/crc32.c"
)
