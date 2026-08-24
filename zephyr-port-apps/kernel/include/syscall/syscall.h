/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <time.h>

struct tm *sys_localtime_r(const time_t *timep, struct tm *result);
bool sys_app_is_watchface(void);
