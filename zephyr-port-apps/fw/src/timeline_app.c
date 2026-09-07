/* SPDX-License-Identifier: Apache-2.0 */

// Timeline Future/Past apps for the launcher. Lists pins from pin_db as a
// MenuLayer (title + time), split into future (timestamp >= now) and past;
// SELECT opens the pin as its real layout card (layout_create, e.g.
// alarm_layout) in a SwapLayer, so UP/DOWN swipe between the pins without
// leaving the card view — the same interaction the shipping timeline shows.

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "applib/ui/layer.h"
#include "applib/ui/menu_layer.h"
#include "applib/ui/menu_cell_layer.h"
#include "applib/ui/window.h"
#include "kernel/pbl_malloc.h"
#include "pbl/services/blob_db/pin_db.h"
#include "pbl/services/blob_db/timeline_item_storage.h"
#include "pbl/services/settings/settings_file.h"
#include "pbl/services/timeline/attribute.h"
#include "pbl/services/timeline/item.h"
#include "pbl/services/timeline/layout_layer.h"
#include "pbl/services/timeline/swap_layer.h"
#include "pbl/services/timeline/timeline_layout.h"

extern Window *window_create(void);
extern void app_window_stack_push(struct Window *window, bool animated);
extern void app_event_loop(void);
extern time_t rtc_get_time(void);
extern size_t clock_copy_time_string_timestamp(char *buffer, uint8_t size, time_t timestamp);
extern time_t time_util_get_midnight_of(time_t ts);

#define MAX_LISTED 24

static bool s_want_future;
static int s_count;
static char s_title[MAX_LISTED][64];
static char s_when[MAX_LISTED][32];
static char s_subtitle[MAX_LISTED][64];
static Uuid s_uuid[MAX_LISTED];
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
    strncpy(s_subtitle[s_count], attribute_get_string(&item.attr_list, AttributeIdSubtitle, empty),
            sizeof(s_subtitle[0]) - 1);
    clock_copy_time_string_timestamp(s_when[s_count], sizeof(s_when[0]), ts);
    s_uuid[s_count] = item.header.id;
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

// SELECT opens the pin as a real layout card (alarm_layout via layout_create),
// the same card the shipping timeline shows. The card view is a SwapLayer:
// UP/DOWN swipe between the listed pins without leaving the card, like the
// shipping timeline. Cards for every listed pin are built up front on open.
// ponytail: precreate all cards (<=MAX_LISTED); a lazy/windowed vend would only
// matter for very long pin lists, which the watch does not have.
static TimelineItem s_items[MAX_LISTED];
static TimelineLayoutInfo s_infos[MAX_LISTED];
static LayoutLayer *s_layouts[MAX_LISTED];
static int s_card_count;
static SwapLayer s_swap;
static int s_idx;

static void prv_free_cards(void) {
  for (int i = 0; i < s_card_count; i++) {
    if (s_layouts[i]) {
      layout_destroy(s_layouts[i]);
      s_layouts[i] = NULL;
    }
    timeline_item_free_allocated_buffer(&s_items[i]);
  }
  s_card_count = 0;
}

static LayoutLayer *prv_get_layout(SwapLayer *sl, int8_t rel, void *ctx) {
  (void)sl;
  (void)ctx;
  const int i = s_idx + rel;
  if (i < 0 || i >= s_card_count || !s_layouts[i]) {
    return NULL;
  }
  return s_layouts[i];
}

static void prv_layout_did_appear(SwapLayer *sl, LayoutLayer *l, int8_t rel, void *ctx) {
  (void)sl;
  (void)l;
  (void)ctx;
  s_idx += rel;
}

static void prv_card_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds;
  layer_get_bounds(root, &bounds);
  swap_layer_init(&s_swap, &bounds);
  swap_layer_set_callbacks(&s_swap, NULL, (SwapLayerCallbacks) {
    .get_layout_handler = prv_get_layout,
    .layout_did_appear_handler = prv_layout_did_appear,
  });
  layer_add_child(root, swap_layer_get_layer(&s_swap));
  swap_layer_set_click_config_onto_window(&s_swap, window);
}

static void prv_card_window_unload(Window *window) {
  (void)window;
  swap_layer_deinit(&s_swap);
  prv_free_cards();
}

static void prv_select(MenuLayer *ml, MenuIndex *idx, void *ctx) {
  (void)ml;
  (void)ctx;
  if (s_count == 0) {
    return;
  }
  prv_free_cards();

  Window *window = window_create();
  Layer *root = window_get_root_layer(window);
  GRect bounds;
  layer_get_bounds(root, &bounds);
  const time_t midnight = time_util_get_midnight_of(rtc_get_time());
  for (int i = 0; i < s_count; i++) {
    if (pin_db_get(&s_uuid[i], &s_items[i]) != S_SUCCESS) {
      continue;
    }
    timeline_layout_init_info(&s_infos[i], &s_items[i], midnight);
    const LayoutLayerConfig config = {
      .frame = &bounds,
      .attributes = &s_items[i].attr_list,
      .mode = LayoutLayerModeCard,
      .app_id = &s_infos[i].app_id,
      .context = &s_infos[i],
    };
    s_layouts[i] = layout_create(s_items[i].header.layout, &config);
  }
  s_card_count = s_count;
  s_idx = idx->row;

  window_set_window_handlers(window, &(WindowHandlers) {
    .load = prv_card_window_load,
    .unload = prv_card_window_unload,
  });
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
