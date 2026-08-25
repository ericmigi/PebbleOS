/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

// NimBLE's os/util.h duplicates these helpers with slightly different macro
// bodies. Use Zephyr's definitions consistently throughout this application.
#include <zephyr/sys/util.h>

#define H_OS_UTIL_
