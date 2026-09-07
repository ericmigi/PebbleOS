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
static char s_subtitle[MAX_LISTED][64];
static char s_detail_buf[192];
static MenuLayer s_menu;
static ScrollLayer s_detail_scroll;
static TextLayer s_detail_text;

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
    strncpy(s_subtitle[s_count], attribute_get_string(&item.attr_list, AttributeIdSubtitle, empty),
            sizeof(s_subtitle[0]) - 1);
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

// SELECT opens a scrollable detail: title, time, and subtitle (alarm kind etc.).
static void prv_select(MenuLayer *ml, MenuIndex *idx, void *ctx) {
  (void)ml;
  (void)ctx;
  if (s_count == 0) {
    return;
  }
  const int r = idx->row;
  snprintf(s_detail_buf, sizeof(s_detail_buf), "%s\n%s%s%s", s_title[r], s_when[r],
           s_subtitle[r][0] ? "\n" : "", s_subtitle[r]);

  Window *window = window_create();
  Layer *root = window_get_root_layer(window);
  GRect bounds;
  layer_get_bounds(root, &bounds);
  scroll_layer_init(&s_detail_scroll, &bounds);
  const GRect tf = GRect(4, 0, bounds.size.w - 8, 2000);
  text_layer_init(&s_detail_text, &tf);
  text_layer_set_font(&s_detail_text, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text(&s_detail_text, s_detail_buf);
  int h = 200;
  if (h < bounds.size.h) {
    h = bounds.size.h;
  }
  text_layer_set_size(&s_detail_text, GSize(bounds.size.w - 8, h));
  scroll_layer_add_child(&s_detail_scroll, text_layer_get_layer(&s_detail_text));
  scroll_layer_set_content_size(&s_detail_scroll, GSize(bounds.size.w, h));
  scroll_layer_set_click_config_onto_window(&s_detail_scroll, window);
  layer_add_child(root, scroll_layer_get_layer(&s_detail_scroll));
  app_window_stack_push(window, true);
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
    .select_click = prv_select,
  };
  menu_layer_set_callbacks(&s_menu, NULL, &cbs);
  menu_layer_set_click_config_onto_window(&s_menu, window);
  layer_add_child(root, menu_layer_get_layer(&s_menu));

  app_window_stack_push(window, true);
  app_event_loop();
}

void fw_timeline_future_app_main(void) { prv_run(true); }
void fw_timeline_past_app_main(void) { prv_run(false); }
