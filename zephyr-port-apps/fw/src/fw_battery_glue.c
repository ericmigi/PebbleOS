/* SPDX-License-Identifier: Apache-2.0 */

// Port battery state. qemu has no battery model, so the state is set over the
// qemu-serial battery endpoint (see qemu_notif_rx.c) and answered here; the
// status bar and the Settings glance read it via battery_state_service_peek().
// Replaces the fixed 100%/unplugged stub that lived in shell_glue.c.
// ponytail: level + charging/plugged flags only; no real fuel gauge or charger.

#include <stdbool.h>
#include <stdint.h>

#include "applib/battery_state_service.h"

static uint8_t s_percent = 100;
static bool s_charging;
static bool s_plugged;

// Called by the qemu-serial battery endpoint.
void fw_battery_set(uint8_t percent, bool charging, bool plugged) {
  s_percent = percent > 100 ? 100 : percent;
  s_charging = charging;
  s_plugged = plugged;
}

BatteryChargeState battery_state_service_peek(void) {
  return (BatteryChargeState) {
    .charge_percent = s_percent,
    .is_charging = s_charging,
    .is_plugged = s_plugged,
  };
}
