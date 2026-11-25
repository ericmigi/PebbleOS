# SPDX-License-Identifier: Apache-2.0
pbl_library()
pbl_library_sources(esp32s3.c shell.c ${PBL_BASE}/third_party/petitfs/pff.c)
pbl_library_include_directories(${PBL_BASE}/third_party/petitfs)
