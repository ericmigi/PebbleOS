/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

// Zephyr and Pebble both expose sign_extend() with different signatures.
// Load Zephyr's definition under a private name before the graphics headers.
#define sign_extend zephyr_sign_extend
#include <zephyr/kernel.h>
#undef sign_extend

#include "../../watchface_sandboxed/include/process_state/app_state/app_state.h"
