/* SPDX-License-Identifier: Apache-2.0 */

// Minimal notifications-history app for the launcher's "Notifications" entry.
// The shipping apps/system/notifications.c pulls the whole notification_window
// (modal manager, presented-list, ancs, imaging); the port instead reads the
// real notification_storage and shows the history as a scrollable text list.
// ponytail: text list, not the shipping menu of cards with per-item actions.
// Upgrade by porting notification_window when the modal stack lands.

#include <stdio.h>
#include <string.h>

#include "applib/fonts/fonts.h"
#include "applib/ui/layer.h"
#include "applib/ui/scroll_layer.h"
#include "applib/ui/text_layer.h"
#include "applib/ui/window.h"
#include "kernel/pbl_malloc.h"
#include "pbl/services/timeline/item.h"
#include "pbl/services/timeline/attribute.h"
#include "pbl/services/notifications/notification_storage.h"

extern Window *window_create(void);
extern void app_window_stack_push(struct Window *window, bool animated);
extern void app_event_loop(void);

#define MAX_LISTED 24

static ScrollLayer s_scroll;
static TextLayer s_text;
static char s_buf[2048];
static Uuid s_uuids[MAX_LISTED];
static int s_uuid_count;

static bool prv_iter_cb(void *data, SerializedTimelineItemHeader *hdr) {
  (void)data;
  if (s_uuid_count < MAX_LISTED) {
    s_uuids[s_uuid_count++] = hdr->common.id;
  }
  return true;  // keep iterating (collect newest at the end)
}

void fw_notifications_app_main(void) {
  s_uuid_count = 0;
  notification_storage_iterate(prv_iter_cb, NULL);

  size_t off = 0;
  s_buf[0] = '\0';
  if (s_uuid_count == 0) {
    off += snprintf(s_buf, sizeof(s_buf), "No notifications");
  } else {
    char empty[] = "";
    // Newest first (iterate yields oldest -> newest).
    for (int i = s_uuid_count - 1; i >= 0 && off < sizeof(s_buf) - 1; i--) {
      TimelineItem item;
      if (!notification_storage_get(&s_uuids[i], &item)) {
        continue;
      }
      const char *title = attribute_get_string(&item.attr_list, AttributeIdTitle, empty);
      const char *body = attribute_get_string(&item.attr_list, AttributeIdBody, empty);
      off += snprintf(s_buf + off, sizeof(s_buf) - off, "%s\n%s\n\n", title, body);
      timeline_item_free_allocated_buffer(&item);
    }
  }

  Window *window = window_create();
  Layer *root = window_get_root_layer(window);
  GRect bounds;
  layer_get_bounds(root, &bounds);

  scroll_layer_init(&s_scroll, &bounds);
  const GRect text_frame = GRect(4, 0, bounds.size.w - 8, 4000);
  text_layer_init(&s_text, &text_frame);
  text_layer_set_font(&s_text, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text(&s_text, s_buf);
  // Rough content height (avoid app graphics-context measurement): ~2 lines +
  // spacing per notification, min one screen.
  int content_h = (s_uuid_count ? s_uuid_count : 1) * 60 + 20;
  if (content_h < bounds.size.h) {
    content_h = bounds.size.h;
  }
  text_layer_set_size(&s_text, GSize(bounds.size.w - 8, content_h));
  scroll_layer_add_child(&s_scroll, text_layer_get_layer(&s_text));
  scroll_layer_set_content_size(&s_scroll, GSize(bounds.size.w, content_h));
  scroll_layer_set_click_config_onto_window(&s_scroll, window);
  layer_add_child(root, scroll_layer_get_layer(&s_scroll));

  app_window_stack_push(window, true);
  app_event_loop();
}
