/* SPDX-License-Identifier: Apache-2.0 */

// Notifications-history app for the launcher's "Notifications" entry. The
// shipping apps/system/notifications.c pulls the whole notification_window
// (modal manager, presented-list, ANCS, imaging); the port instead reads the
// real notification_storage and shows the history as a MenuLayer of
// title/subtitle rows. SELECT opens a scrollable detail with the body.
// ponytail: no per-item dismiss/actions yet (shipping has an action menu per
// notification); upgrade when notification_window is ported.

#include <stdio.h>
#include <string.h>

#include "applib/fonts/fonts.h"
#include "applib/ui/layer.h"
#include "applib/ui/click.h"
#include "applib/ui/menu_layer.h"
#include "applib/ui/menu_cell_layer.h"
#include "applib/ui/scroll_layer.h"
#include "applib/ui/text_layer.h"
#include "applib/ui/window.h"
#include "kernel/pbl_malloc.h"
#include "pbl/services/timeline/item.h"
#include "pbl/services/timeline/attribute.h"
#include "pbl/services/notifications/notification_storage.h"

extern Window *window_create(void);
extern void app_window_stack_push(struct Window *window, bool animated);
extern Window *app_window_stack_pop(bool animated);
extern void window_single_click_subscribe(ButtonId button_id, ClickHandler handler);
extern void app_event_loop(void);

#define MAX_LISTED 24

static Uuid s_uuids[MAX_LISTED];
static int s_count;
static char s_title[MAX_LISTED][64];
static char s_subtitle[MAX_LISTED][96];
static char s_body[MAX_LISTED][256];
static Uuid s_row_uuid[MAX_LISTED];

static MenuLayer s_menu;
static ScrollLayer s_detail_scroll;
static TextLayer s_detail_text;

static bool prv_iter_cb(void *data, SerializedTimelineItemHeader *hdr) {
  (void)data;
  if (s_count < MAX_LISTED) {
    s_uuids[s_count++] = hdr->common.id;
  }
  return true;  // keep iterating (oldest -> newest)
}

// Cache the stored notifications newest-first into the row arrays.
static void prv_load(void) {
  s_count = 0;
  notification_storage_iterate(prv_iter_cb, NULL);
  char empty[] = "";
  const int n = s_count;
  Uuid tmp[MAX_LISTED];
  memcpy(tmp, s_uuids, sizeof(Uuid) * n);
  int out = 0;
  for (int i = n - 1; i >= 0; i--) {
    TimelineItem item;
    if (!notification_storage_get(&tmp[i], &item)) {
      continue;
    }
    strncpy(s_title[out], attribute_get_string(&item.attr_list, AttributeIdTitle, empty),
            sizeof(s_title[0]) - 1);
    strncpy(s_subtitle[out], attribute_get_string(&item.attr_list, AttributeIdSubtitle, empty),
            sizeof(s_subtitle[0]) - 1);
    strncpy(s_body[out], attribute_get_string(&item.attr_list, AttributeIdBody, empty),
            sizeof(s_body[0]) - 1);
    s_row_uuid[out] = tmp[i];
    timeline_item_free_allocated_buffer(&item);
    out++;
  }
  s_count = out;
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
    menu_cell_basic_draw(gctx, cell, "No notifications", NULL, NULL);
    return;
  }
  const int r = idx->row;
  menu_cell_basic_draw(gctx, cell, s_title[r], s_subtitle[r][0] ? s_subtitle[r] : NULL, NULL);
}

static int s_sel_row;  // row whose detail is currently open

// SELECT inside the detail view dismisses that notification: remove it from the
// store, pop back to the list, reload the menu. UP/DOWN still scroll the body;
// BACK returns without dismissing. (Long-SELECT on the list row is not usable —
// a held key is not injectable in qemu — so dismiss lives on the detail SELECT.)
static void prv_detail_dismiss(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
  (void)context;
  notification_storage_remove(&s_row_uuid[s_sel_row]);
  app_window_stack_pop(true);
  prv_load();
  menu_layer_reload_data(&s_menu);
}

static void prv_detail_click_config(void *context) {
  (void)context;
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_detail_dismiss);
}

static void prv_select(MenuLayer *ml, MenuIndex *idx, void *ctx) {
  (void)ml;
  (void)ctx;
  if (s_count == 0) {
    return;
  }
  s_sel_row = idx->row;
  const int r = s_sel_row;

  Window *window = window_create();
  Layer *root = window_get_root_layer(window);
  GRect bounds;
  layer_get_bounds(root, &bounds);
  scroll_layer_init(&s_detail_scroll, &bounds);
  const GRect tf = GRect(4, 0, bounds.size.w - 8, 2000);
  text_layer_init(&s_detail_text, &tf);
  text_layer_set_font(&s_detail_text, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text(&s_detail_text, s_body[r][0] ? s_body[r] : s_title[r]);
  int h = 400;
  if (h < bounds.size.h) {
    h = bounds.size.h;
  }
  text_layer_set_size(&s_detail_text, GSize(bounds.size.w - 8, h));
  scroll_layer_add_child(&s_detail_scroll, text_layer_get_layer(&s_detail_text));
  scroll_layer_set_content_size(&s_detail_scroll, GSize(bounds.size.w, h));
  const ScrollLayerCallbacks scb = { .click_config_provider = prv_detail_click_config };
  scroll_layer_set_callbacks(&s_detail_scroll, scb);
  scroll_layer_set_click_config_onto_window(&s_detail_scroll, window);
  layer_add_child(root, scroll_layer_get_layer(&s_detail_scroll));
  app_window_stack_push(window, true);
}

void fw_notifications_app_main(void) {
  prv_load();

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
