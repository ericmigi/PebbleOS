/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <zephyr/sys/__assert.h>

#define PBL_ASSERTN(condition) __ASSERT_NO_MSG(condition)
#define PBL_ASSERT(condition, fmt, ...) __ASSERT(condition, fmt, ##__VA_ARGS__)
#define PBL_CROAK(fmt, ...) __ASSERT(false, fmt, ##__VA_ARGS__)
#define WTF __ASSERT_NO_MSG(false)
