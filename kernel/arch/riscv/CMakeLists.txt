# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

pbl_library_sources(
  arch.c
  atomic.c
  fault.c
  irq.c
  trap.S
)

pbl_include_directories(include)
pbl_linker_include_directories(linker)
