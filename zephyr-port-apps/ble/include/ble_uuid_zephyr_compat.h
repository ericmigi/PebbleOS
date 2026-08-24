/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

/* Zephyr exports a four-argument hex2bin. NimBLE has a private, static
 * three-argument helper with the same name. Include Zephyr's declaration
 * before locally renaming only NimBLE's helper. */
#include "nimble_zephyr_compat.h"

#define hex2bin nimble_ble_uuid_hex2bin
