/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

typedef enum {
  APP_LOG_LEVEL_ERROR = 1,
  APP_LOG_LEVEL_WARNING = 50,
  APP_LOG_LEVEL_INFO = 100,
  APP_LOG_LEVEL_DEBUG = 200,
} AppLogLevel;

#define APP_LOG(level, fmt, args...) do { (void)(level); } while (0)
