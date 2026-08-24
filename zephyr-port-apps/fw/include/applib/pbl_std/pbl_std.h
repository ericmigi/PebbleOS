/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

// Shadow of applib/pbl_std/pbl_std.h for the Zephyr scaffold. The real header
// pulls in util/time/time.h, whose unguarded `struct tm` clashes with the
// Zephyr minimal-libc <time.h>. menu_layer.c is the only UI file that includes
// pbl_std and it uses none of its symbols, so an empty shim resolves the
// include without dragging in the conflicting time definitions.
