/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdint.h>

#define RESOURCE_ID_FONT_FALLBACK_INTERNAL 1u

// The generated shipping header defines this; the applib dialog headers (used by
// the Alarms app) type icon parameters as ResourceId.
typedef uint32_t ResourceId;
