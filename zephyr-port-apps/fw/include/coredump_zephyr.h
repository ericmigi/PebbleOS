/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>

#include <zephyr/arch/cpu.h>
#include <zephyr/toolchain.h>

//! Emit an unread PebbleOS coredump as hex-framed UART records.
//! Returns true when any valid coredump is present, including an already-read one.
bool coredump_zephyr_emit_uart(void);

//! Deliberately generate a CPU fault when PEBBLE_COREDUMP_TEST_FAULT is enabled.
void coredump_zephyr_test_fault(void);

//! Capture the supplied Zephyr fatal frame with the PebbleOS writer and reset.
FUNC_NORETURN void coredump_zephyr_capture_fatal(
    unsigned int reason, const struct arch_esf *esf);
