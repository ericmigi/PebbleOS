# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

pbl_library()
pbl_include_directories(.)
pbl_library_sources(
  init.c
  intmatrix.c
)
pbl_library_sources_ifdef(CONFIG_CRC retained.c)
pbl_library_sources_ifdef(CONFIG_SOC_ESP32C3_POWER_OFF power_off.c)
