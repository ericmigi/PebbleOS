/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <time.h>

#include "FreeRTOS.h"
#include "app_registry.h"
#include "applib/tick_timer_service.h"
#include "applib/tick_timer_service_private.h"
#include "kernel/event_loop.h"
#include "kernel/events.h"
#include "kernel/kernel_applib_state.h"
#include "kernel/pebble_tasks.h"
#include "kernel/util/task_init.h"
#include "pbl/logging/logging.h"
#include "pbl/services/event_service.h"
#include "pbl/services/new_timer/new_timer.h"
#include "pbl/services/new_timer/new_timer_service.h"
#include "pbl/services/regular_timer.h"
#include "pbl/services/system_task.h"
#include "pfs_boot.h"
#include "sandbox_launcher.h"

static TimerID s_probe_timer;

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

  TickTimerServiceState *tick_state = kernel_applib_get_tick_timer_service_state();
  tick_timer_service_state_init(tick_state);
  tick_timer_service_init();
  tick_timer_service_subscribe(SECOND_UNIT, prv_tick_handler);

  s_probe_timer = new_timer_create();
  new_timer_start(s_probe_timer, 1500, prv_timer_fired, NULL, 0);

  PBL_LOG_ALWAYS("FW_SERVICES_OK");
  if (fw_pfs_boot() == 0) {
    PBL_LOG_ALWAYS("FW_PFS_UP");
    fw_app_registry_init();
    const FwAppRegistryEntry *app = fw_launcher_pick_app();
    if (app) {
      PBL_LOG_ALWAYS("FW_LAUNCH %s", app->name);
      (void)fw_sandbox_launch();
    }
  }
  launcher_main_loop();
}

static void prv_log_stubs(void) {
  PBL_LOG_ALWAYS("FW_STUB board_drivers");
  PBL_LOG_ALWAYS("FW_STUB display_compositor");
  PBL_LOG_ALWAYS("FW_STUB ble_comm");
  PBL_LOG_ALWAYS("FW_STUB watchdog_analytics");
}

int main(void) {
  PBL_LOG_ALWAYS("FW_BOOT");
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
