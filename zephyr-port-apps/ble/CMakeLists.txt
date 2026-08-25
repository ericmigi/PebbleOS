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

target_compile_definitions(app PRIVATE
  CONFIG_BOARD_OBELIX_PVT=1
  CONFIG_BOARD_OBELIX=1
  CONFIG_BT_LOG_LEVEL=3
  CONFIG_SOC_SF32LB52=1
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
