/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdint.h>

int sandbox_syscall_probe(int value);
void sandbox_app_event_loop(void);
