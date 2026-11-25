# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

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
