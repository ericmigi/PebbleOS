/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

// Port stub shadowing shipping src/fw/util/time/time.h. The shipping header
// redeclares localtime_r/gmtime_r/localtime/gmtime WITHOUT the `restrict`
// qualifiers Zephyr's minimal libc uses, which the compiler rejects as
// conflicting types (and drags in a second `struct tm`). No port source needs
// the shipping extras (TimezoneInfo, time_t_to_string, ...); a system app only
// wants the standard time.h types. A future ported app that needs those extras
// should reconcile the restrict qualifiers in the real header instead of adding
// them here.
#include <stdint.h>
#include <time.h>

// A couple of shipping time helpers the ported applib UI needs, kept here so the
// rest of the shipping header (with its conflicting restrict-less redeclarations)
// stays shadowed out. Implemented in fw/src/app_service_stubs.c.
uint16_t time_ms(time_t *tloc, uint16_t *out_ms);
