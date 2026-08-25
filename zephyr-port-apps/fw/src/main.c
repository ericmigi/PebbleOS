/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <time.h>

#include "FreeRTOS.h"
#include "app_registry.h"
#include "applib/tick_timer_service.h"
#include "applib/tick_timer_service_private.h"
#include "button_input.h"
#include "kernel/event_loop.h"
#include "kernel/events.h"
#include "kernel/kernel_applib_state.h"
#include "kernel/pebble_tasks.h"
#include "kernel/util/task_init.h"
#include "launcher_ui.h"
#include "pbl/drivers/task_watchdog.h"
#include "pbl/logging/logging.h"
#include "pbl/services/analytics/analytics.h"
#include "pbl/services/event_service.h"
#include "pbl/services/new_timer/new_timer.h"
#include "pbl/services/new_timer/new_timer_service.h"
#include "pbl/services/regular_timer.h"
#include "pbl/services/system_task.h"
#include "pfs_boot.h"
#include "sandbox_launcher.h"

void board_early_init(void);
void board_init(void);

static TimerID s_probe_timer;

static void prv_watchdog_timer_callback(void *data) {
  (void)data;
  task_watchdog_bit_set(PebbleTask_NewTimers);
}

static RegularTimerInfo s_watchdog_timer = {
    .cb = prv_watchdog_timer_callback,
};

static void prv_timer_dispatched(void *data) {
  (void)data;
  PBL_LOG_ALWAYS("FW_TIMER dispatched");
}

static void prv_timer_fired(void *data) {
  (void)data;
  launcher_task_add_callback(prv_timer_dispatched, NULL);
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  (void)units_changed;
  PBL_LOG_ALWAYS("FW_TICK %02d:%02d:%02d", tick_time->tm_hour, tick_time->tm_min,
                 tick_time->tm_sec);
}

static void prv_kernel_main(void *parameter) {
  (void)parameter;
  task_init();

  task_watchdog_init();
  task_watchdog_pause(30);
  regular_timer_add_seconds_callback(&s_watchdog_timer);
  pbl_analytics_init();
  PBL_ANALYTICS_SET_UNSIGNED(uptime_s, 0U);

  board_init();

  TickTimerServiceState *tick_state = kernel_applib_get_tick_timer_service_state();
  tick_timer_service_state_init(tick_state);
  tick_timer_service_init();
  tick_timer_service_subscribe(SECOND_UNIT, prv_tick_handler);

  s_probe_timer = new_timer_create();
  new_timer_start(s_probe_timer, 1500, prv_timer_fired, NULL, 0);

  PBL_LOG_ALWAYS("FW_SERVICES_OK");

  // Buttons: prove the debounce filter, then bring up the real click_recognizer
  // and start sampling the physical GPIOs. Must run on KernelMain so click.c's
  // app_timer callbacks dispatch back on this task via the event loop.
  button_input_selfcheck();
  input_service_init();
  button_zephyr_init();

  if (fw_pfs_boot() == 0) {
    PBL_LOG_ALWAYS("FW_PFS_UP");
    fw_app_registry_init();
  }

  task_watchdog_mask_set(PebbleTask_KernelMain);
  task_watchdog_resume();
  PBL_LOG_ALWAYS("FW_WATCHDOG_OK");
  PBL_LOG_ALWAYS("FW_ANALYTICS_OK");

  // Stand up the real window stack + launcher menu and run the KernelMain UI
  // event loop. Navigation consumes the button/click-service events wired above
  // (button_zephyr -> event queue -> input_service click_recognizer); SELECT
  // launches the selected app. Replaces the previous auto-launch + bare
  // launcher_main_loop().
  fw_launcher_ui_run();
}

static void prv_log_stubs(void) {
  // ponytail: the port writes Pebble framebuffers directly with Zephyr's JDI
  // display driver. Add the real compositor and kernel-ui service graph here.
  PBL_LOG_ALWAYS("FW_STUB display_compositor");
  // ponytail: BLE currently runs in zephyr-port-apps/ble as a separate image.
  // Add the inter-core transport, then bind Pebble comm sessions in this app.
  PBL_LOG_ALWAYS("FW_STUB ble_comm");
}

int main(void) {
  PBL_LOG_ALWAYS("FW_BOOT");
  board_early_init();
  prv_log_stubs();

  events_init();
  event_service_system_init();
  new_timer_service_init();
  regular_timer_init();
  system_task_init();

  TaskParameters_t main_task = {
    .pvTaskCode = prv_kernel_main,
    .pcName = "KernelMain",
    .usStackDepth = 6144 / sizeof(portSTACK_TYPE),
    .uxPriority = tskIDLE_PRIORITY + 3,
  };
  pebble_task_create(PebbleTask_KernelMain, &main_task, NULL);
  return 0;
}
