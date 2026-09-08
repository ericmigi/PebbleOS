/* SPDX-License-Identifier: Apache-2.0 */

// Minimal digital watchface for the Zephyr port: a centered HH:MM TextLayer
// refreshed each minute. Exists as a SECOND ProcessTypeWatchface so the
// Watchfaces picker can actually switch the running face (the shipping built-in
// faces low_power/kickstart do not compile in the port — sign_extend header
// conflict). Include set mirrors tictoc, which compiles clean.

// Zephyr and Pebble both declare sign_extend() with different signatures; load
// Zephyr's under a private name before the Pebble headers (mirrors launcher_ui.c).
#define sign_extend zephyr_sign_extend
#include <zephyr/kernel.h>
#undef sign_extend

#include "applib/app.h"
#include "applib/tick_timer_service.h"
#include "applib/ui/app_window_stack.h"
#include "applib/ui/ui.h"
#include "applib/fonts/fonts.h"
#include "kernel/pbl_malloc.h"
#include "process_state/app_state/app_state.h"
#include "process_management/pebble_process_md.h"
#include "pbl/services/clock.h"
#include "resource/resource_ids.auto.h"

#include <string.h>
#include <time.h>

extern void rtc_get_time_tm(struct tm *time_tm);
extern void digital_main(void);
extern size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm);  // shell_glue.c

typedef struct {
  Window window;
  TextLayer time_layer;
  char text[8];
} DigitalData;

static void prv_tick(struct tm *tick_time, TimeUnits units_changed) {
  (void)units_changed;
  DigitalData *data = app_state_get_user_data();
  strftime(data->text, sizeof(data->text), clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);
  text_layer_set_text(&data->time_layer, data->text);
  layer_mark_dirty(&data->time_layer.layer);
}

static void prv_window_load(Window *window) {
  DigitalData *data = window_get_user_data(window);
  Layer *root = window_get_root_layer(window);
  GRect bounds = root->bounds;

  // Vertically-centered band for the time text.
  const int16_t h = 40;
  const GRect frame = GRect(0, (bounds.size.h - h) / 2, bounds.size.w, h);
  text_layer_init(&data->time_layer, &frame);
  text_layer_set_background_color(&data->time_layer, GColorClear);
  text_layer_set_text_color(&data->time_layer, GColorWhite);
  text_layer_set_text_alignment(&data->time_layer, GTextAlignmentCenter);
  text_layer_set_font(&data->time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  layer_add_child(root, &data->time_layer.layer);
}

static void prv_init(void) {
  DigitalData *data = app_zalloc_check(sizeof(DigitalData));
  app_state_set_user_data(data);

  window_init(&data->window, WINDOW_NAME("Digital"));
  window_set_background_color(&data->window, GColorBlack);
  window_set_user_data(&data->window, data);
  window_set_window_handlers(&data->window, &(WindowHandlers){ .load = prv_window_load });
  app_window_stack_push(&data->window, true);

  struct tm now;
  rtc_get_time_tm(&now);
  prv_tick(&now, MINUTE_UNIT);
  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick);
}

static void prv_deinit(void) {
  DigitalData *data = app_state_get_user_data();
  tick_timer_service_unsubscribe();
  text_layer_deinit(&data->time_layer);
  window_deinit(&data->window);
  app_free(data);
}

void digital_main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}

const PebbleProcessMd *digital_face_get_app_info(void) {
  static const PebbleProcessMdSystem s_md = {
    .common = {
      // UUID: d1917a10-0000-4000-8000-646967697400 ("digit")
      .uuid = { 0xd1, 0x91, 0x7a, 0x10, 0x00, 0x00, 0x40, 0x00,
                0x80, 0x00, 0x64, 0x69, 0x67, 0x69, 0x74, 0x00 },
      .main_func = digital_main,
      .process_type = ProcessTypeWatchface,
    },
    .icon_resource_id = RESOURCE_ID_MENU_ICON_TICTOC_WATCH,
    .name = "Digital",
  };
  return (const PebbleProcessMd *)&s_md;
}
