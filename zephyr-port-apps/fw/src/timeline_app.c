/* SPDX-License-Identifier: Apache-2.0 */

// Minimal Timeline Future/Past apps for the launcher. The shipping timeline app
// renders pins as layout cards (layout_layer + all 7 layout modules, a large
// port); this instead lists pins from pin_db as a MenuLayer (title + time),
// split into future (timestamp >= now) and past. Makes alarm pins visible
// (alarm_pin writes them to pin_db) without the render-side port.
// ponytail: text/menu list, not the shipping pin cards; upgrade with
// layout_layer when the render side is ported.

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "applib/fonts/fonts.h"
#include "applib/ui/layer.h"
#include "applib/ui/menu_layer.h"
#include "applib/ui/menu_cell_layer.h"
#include "applib/ui/scroll_layer.h"
#include "applib/ui/text_layer.h"
#include "applib/ui/window.h"
#include "kernel/pbl_malloc.h"
#include "pbl/services/blob_db/pin_db.h"
#include "pbl/services/blob_db/timeline_item_storage.h"
#include "pbl/services/settings/settings_file.h"
#include "pbl/services/timeline/attribute.h"
#include "pbl/services/timeline/item.h"

extern Window *window_create(void);
extern void app_window_stack_push(struct Window *window, bool animated);
extern void app_event_loop(void);
extern time_t rtc_get_time(void);
extern size_t clock_copy_time_string_timestamp(char *buffer, uint8_t size, time_t timestamp);

#define MAX_LISTED 24

static bool s_want_future;
static int s_count;
static char s_title[MAX_LISTED][64];
static char s_when[MAX_LISTED][32];
static MenuLayer s_menu;

static bool prv_pin_cb(SettingsFile *file, SettingsRecordInfo *info, void *ctx) {
  (void)ctx;
  if (s_count >= MAX_LISTED) {
    return false;
  }
  TimelineItem item;
  if (timeline_item_storage_get_from_settings_record(file, info, &item) != S_SUCCESS) {
    return true;
  }
  const time_t ts = item.header.timestamp;
  const bool is_future = ts >= rtc_get_time();
  if (is_future == s_want_future) {
    char empty[] = "";
    strncpy(s_title[s_count], attribute_get_string(&item.attr_list, AttributeIdTitle, empty),
            sizeof(s_title[0]) - 1);
    clock_copy_time_string_timestamp(s_when[s_count], sizeof(s_when[0]), ts);
    s_count++;
  }
  timeline_item_free_allocated_buffer(&item);
  return true;
}

static uint16_t prv_num_rows(MenuLayer *ml, uint16_t section, void *ctx) {
  (void)ml;
  (void)section;
  (void)ctx;
  return s_count ? s_count : 1;
}

static void prv_draw_row(GContext *gctx, const Layer *cell, MenuIndex *idx, void *ctx) {
  (void)ctx;
  if (s_count == 0) {
    menu_cell_basic_draw(gctx, cell, s_want_future ? "No future pins" : "No past pins", NULL, NULL);
    return;
  }
  const int r = idx->row;
  menu_cell_basic_draw(gctx, cell, s_title[r], s_when[r][0] ? s_when[r] : NULL, NULL);
}

static void prv_run(bool future) {
  s_want_future = future;
  s_count = 0;
  pin_db_each(prv_pin_cb, NULL);

  Window *window = window_create();
  Layer *root = window_get_root_layer(window);
  GRect bounds;
  layer_get_bounds(root, &bounds);
  menu_layer_init(&s_menu, &bounds);
  const MenuLayerCallbacks cbs = {
    .get_num_rows = prv_num_rows,
    .draw_row = prv_draw_row,
  };
  menu_layer_set_callbacks(&s_menu, NULL, &cbs);
  menu_layer_set_click_config_onto_window(&s_menu, window);
  layer_add_child(root, menu_layer_get_layer(&s_menu));

  app_window_stack_push(window, true);
  app_event_loop();
}

void fw_timeline_future_app_main(void) { prv_run(true); }
void fw_timeline_past_app_main(void) { prv_run(false); }
