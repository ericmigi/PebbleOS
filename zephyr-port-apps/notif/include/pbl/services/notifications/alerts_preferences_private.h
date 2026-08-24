/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>

typedef enum {
  NotificationStatusBarStyle_Default = 0,
  NotificationStatusBarStyle_Bold = 1,
  NotificationStatusBarStyle_LargeBold = 2,
} NotificationStatusBarStyle;

bool alerts_preferences_get_notification_alternative_design(void);
NotificationStatusBarStyle alerts_preferences_get_notification_status_bar_style(void);

