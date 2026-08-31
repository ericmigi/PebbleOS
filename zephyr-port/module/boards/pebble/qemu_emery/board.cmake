# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

set(SUPPORTED_EMU_PLATFORMS qemu)
set(QEMU_ARCH arm)
set(QEMU_CPU_TYPE_${ARCH} cortex-m33)
set(QEMU_FLAGS_${ARCH}
  -machine pebble-emery
  -display none
  )
board_set_debugger_ifnset(qemu)
