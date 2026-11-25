# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

pbl_include_directories(samsa/include)
pbl_library_named(samsa)
pbl_library_kind(stlib)
pbl_library_sources(samsa_unity.c)
pbl_library_include_directories(samsa/src)
# The interpreter's loop is where an app's time goes
pbl_library_compile_options(-O2)
# Global: the decode cache is part of the CPU state everything that includes samsa.h lays out
pbl_compile_definitions(SAMSA_CACHE_ENTRIES=${CONFIG_SAMSA_CACHE_ENTRIES})
