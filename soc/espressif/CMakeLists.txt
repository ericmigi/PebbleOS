# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

pbl_library()
pbl_include_directories(include)

if(CONFIG_SOC_ESP32C3)
  add_subdirectory(esp32c3)
endif()
