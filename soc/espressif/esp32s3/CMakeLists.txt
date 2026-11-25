# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

pbl_library()
pbl_include_directories(.)
pbl_library_sources(
  boot_header.c
  init.c
  intmatrix.c
)
pbl_library_sources_ifdef(CONFIG_CRC retained.c)
