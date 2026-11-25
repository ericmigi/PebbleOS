# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

# Espressif's closed libraries come from two submodules, which a plain checkout leaves empty
foreach(lib esp32c3-bt-lib/esp32c3/libbtdm_app.a esp-phy-lib/esp32c3/libphy.a)
  if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${lib})
    message(FATAL_ERROR
      "CONFIG_ESP32C3_BT: ${lib} is missing. Fetch Espressif's libraries with\n"
      "  git submodule update --init --depth 1 -- third_party/espressif_bt/esp32c3-bt-lib "
      "third_party/espressif_bt/esp-phy-lib\n"
      "or build without Bluetooth: -DCONFIG_ESP32C3_BT=n")
  endif()
endforeach()

pbl_library()
pbl_library_sources(
  esp32c3/bt.c
  esp32c3/ctl.c
  esp32c3/phy.c
  esp32c3/phy_init_data.c
)
pbl_library_include_directories(esp32c3)
pbl_include_directories(include)
# The libraries are built with full-size enums
pbl_library_compile_options(-fno-short-enums)

# The controller patched in ROM (ESP-IDF's default), or linked whole in flash
# (BT_CTRL_RUN_IN_FLASH_ONLY), which needs less RAM code but runs its interrupts from flash.
if(CONFIG_ESP32C3_BT_CONTROLLER_IN_FLASH)
  pbl_library_import(btdm_app_flash esp32c3-bt-lib/esp32c3)
else()
  pbl_library_import(btdm_app esp32c3-bt-lib/esp32c3)
  pbl_linker_sources(footer esp32c3/rom_patch.ld)
endif()
pbl_library_import(phy esp-phy-lib/esp32c3)
pbl_library_import(btbb esp-phy-lib/esp32c3)

pbl_linker_sources(ramfunc esp32c3/iram1.ld)
pbl_linker_sources(data esp32c3/dram1.ld)
pbl_linker_sources(footer esp32c3/rom.ld)
