/* SPDX-License-Identifier: Apache-2.0 */

// Port closure + wiring for the real alarm-fired popup (src/fw/popups/
// alarm_popup.c). Shipping's shell_event_loop calls alarm_popup_push_window() on
// PEBBLE_ALARM_CLOCK_EVENT; the port has no shell_event_loop, so subscribe to
// the event here (dispatched on the KernelMain UI pump like every other event
// service client) and forward. The dialog + action bar + vibes are real; only
// the speaker tones and low-power/light services (absent in qemu) are stubbed.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "applib/event_service_client.h"
#include "kernel/events.h"
#include "popups/alarm_popup.h"
#include "pbl/services/alarms/alarm.h"
#include "pbl/services/vibes/vibe_score.h"
#include "services/alarms/alarm_tones.h"

// No speaker in the port: report no tone notes and no paired vibe.
void alarm_tones_get(AlarmTone tone, const SpeakerNote **notes_out, uint32_t *count_out) {
  (void)tone;
  if (notes_out) {
    *notes_out = NULL;
  }
  if (count_out) {
    *count_out = 0;
  }
}

VibeScoreId alarm_tones_get_paired_vibe(AlarmTone tone) {
  (void)tone;
  return VibeScoreId_Invalid;
}

// low-power is not modeled in the port. (light_enable_interaction lives in
// apps_port_glue.c.)
bool low_power_is_active(void) { return false; }

// The port has no haptics; the alarm popup drives these directly.
void vibes_long_pulse(void) {}

static void prv_alarm_event(PebbleEvent *event, void *context) {
  (void)context;
  alarm_popup_push_window(&event->alarm_clock);
}

void fw_alarm_alert_init(void) {
  static EventServiceInfo s_info;
  s_info = (EventServiceInfo){.type = PEBBLE_ALARM_CLOCK_EVENT, .handler = prv_alarm_event};
  event_service_client_subscribe(&s_info);
}
