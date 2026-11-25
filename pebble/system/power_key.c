/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#ifndef CONFIG_MFG

#include <pbl/input/input.h>
#include <pbl/kernel/reboot.h>
#include <pbl/kernel/work.h>

#include <kernel/event_loop.h>

// Held this long, a board's power key powers off
#define POWER_KEY_HOLD_MS 2000U

static void prv_power_off(void *data) {
  pbl_power_off();
}

// On KernelMain, as the Shut Down menu does: the prepare handlers wait on the system queues
static void prv_held(struct pbl_work *work) {
  launcher_task_add_callback(prv_power_off, nullptr);
}

static PBL_DELAYED_WORK_DEFINE(s_hold, prv_held);

static void prv_input_cb(const struct pbl_input_event *evt, void *user_data) {
  if (evt->type != PBL_INPUT_EV_KEY || evt->code != PBL_INPUT_KEY_POWER) {
    return;
  }

  if (evt->value) {
    (void)pbl_delayed_work_reschedule(&pbl_sys_workq, &s_hold, PBL_MSEC(POWER_KEY_HOLD_MS));
  } else {
    (void)pbl_delayed_work_cancel(&s_hold);
  }
}

PBL_INPUT_CALLBACK_DEFINE(prv_input_cb, nullptr);

#endif
