/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

// Slim shadow of applib/app_logging.h: APP_LOG is a no-op in the port (matches
// the gfx app's shadow).
typedef enum {
  APP_LOG_LEVEL_ERROR = 1,
  APP_LOG_LEVEL_WARNING = 50,
  APP_LOG_LEVEL_INFO = 100,
  APP_LOG_LEVEL_DEBUG = 200,
} AppLogLevel;

#define APP_LOG(level, ...)
