/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

// Force-included (via CMake -include) ahead of a real PebbleOS app source that
// pulls BOTH Zephyr (through FreeRTOS.h) and the Pebble graphics/math headers.
// Zephyr's sys/util.h and Pebble's pbl/util/math.h both declare sign_extend()
// with different signatures; pre-include Zephyr with sign_extend renamed so the
// Pebble declaration is the one that wins for the app code. Mirrors the dance in
// launcher_ui.c / sandbox_graphics_state.h.
#define sign_extend zephyr_sign_extend
#include <zephyr/kernel.h>
#undef sign_extend
