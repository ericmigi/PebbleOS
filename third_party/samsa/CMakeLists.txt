# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

set(stock ${CMAKE_CURRENT_SOURCE_DIR}/samsa)
set(prepared ${CMAKE_CURRENT_BINARY_DIR}/source)
set(patches ${CMAKE_CURRENT_SOURCE_DIR}/patches/sdk-dispatch.patch)
if(CONFIG_SAMSA_FAST64)
  list(APPEND patches ${CMAKE_CURRENT_SOURCE_DIR}/patches/fast64.patch)
endif()
file(GLOB inputs CONFIGURE_DEPENDS ${stock}/src/*.[ch] ${stock}/include/samsa/*.h)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${inputs} ${stock}/tests/test_control.c ${patches} ${CMAKE_CURRENT_SOURCE_DIR}/prepare.py)
execute_process(COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/prepare.py
  ${stock} ${prepared} ${patches} COMMAND_ERROR_IS_FATAL ANY)

pbl_include_directories(${prepared}/include)
pbl_library_named(samsa)
pbl_library_kind(stlib)
if(CONFIG_SAMSA_FAST64)
  pbl_library_sources(samsa_unity.c)
else()
  pbl_library_sources(${prepared}/src/samsa.c ${prepared}/src/alu.c ${prepared}/src/mem.c
                      ${prepared}/src/decode16.c ${prepared}/src/decode32.c)
endif()
pbl_library_include_directories(${prepared}/src)
pbl_library_compile_options(-O2)
# The cache is part of the public CPU state layout.
pbl_compile_definitions(SAMSA_CACHE_ENTRIES=${CONFIG_SAMSA_CACHE_ENTRIES})
