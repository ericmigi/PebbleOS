/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#define PBL_ANALYTICS_KEY(key_name) 0
#define PBL_ANALYTICS_SET_SIGNED(key_name, value) ((void)(value))
#define PBL_ANALYTICS_SET_UNSIGNED(key_name, value) ((void)(value))
#define PBL_ANALYTICS_SET_STRING(key_name, value) ((void)(value))
#define PBL_ANALYTICS_TIMER_START(key_name) do { } while (0)
#define PBL_ANALYTICS_TIMER_STOP(key_name) do { } while (0)
#define PBL_ANALYTICS_ADD(key_name, value) ((void)(value))
