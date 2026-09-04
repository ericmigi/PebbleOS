/* SPDX-License-Identifier: Apache-2.0 */

// Notification display on the Zephyr port. The QEMU receive thread
// (qemu_notif_rx.c) or the ANCS decoder (qemu_ancs.c) decodes a notification
// and calls fw_notification_show (thread-safe stash only). fw_notification_poll
// runs on KernelMain from the shared UI pump (fw_ui_pump_once) and renders it.
//
// The card pixels come from the shipping timeline notification_layout: a
// TimelineItem is assembled from the decoded Title/Subtitle/Body, wrapped in a
// LayoutLayer (LayoutLayerModeCard), and mounted on a window pushed onto the
// shared window stack. layer_render_tree walks the layout's node tree the same
// way the reference's notification_window does, so title/body/timestamp match.
//
// ponytail: no modal_manager peek intro animation, no scroll_layer/swap_layer
// (single static card, top-anchored) and no action menu. The item + layout are
// single static slots — one notification on screen at a time, rebuilt each show;
// the previous layout leaks until multi-notification nav is wired. Swap in the
// full notification_window (peek + swap_layer + actions) once modal_manager is
// ported.

#include "applib/graphics/gtypes.h"
#include "applib/ui/app_window_stack.h"
#include "applib/ui/layer.h"
#include "applib/ui/window.h"
#include "pbl/services/timeline/attribute.h"
#include "pbl/services/timeline/item.h"
#include "pbl/services/timeline/layout_layer.h"
#include "pbl/services/timeline/notification_layout.h"
#include "util/uuid.h"

#include <zephyr/sys/printk.h>
#include <string.h>
#include <time.h>

extern time_t rtc_get_time(void);
extern Window *window_create(void);
extern void fw_window_stack_push(Window *window);

static char s_title[64];
static char s_subtitle[64];
static char s_body[128];
static volatile bool s_pending;

static Attribute s_attrs[3];
static TimelineItem s_item;
static NotificationLayoutInfo s_info;
static Uuid s_app_id;  // zeroed = invalid: no phone app icon in the port

void fw_notification_show(const char *title, const char *subtitle, const char *body) {
  strncpy(s_title, title ? title : "", sizeof(s_title) - 1);
  s_title[sizeof(s_title) - 1] = '\0';
  strncpy(s_subtitle, subtitle ? subtitle : "", sizeof(s_subtitle) - 1);
  s_subtitle[sizeof(s_subtitle) - 1] = '\0';
  strncpy(s_body, body ? body : "", sizeof(s_body) - 1);
  s_body[sizeof(s_body) - 1] = '\0';
  s_pending = true;
}

// Called from fw_ui_pump_once (KernelMain). Renders one pending notification.
void fw_notification_poll(void) {
  if (!s_pending) {
    return;
  }
  s_pending = false;

  uint8_t n = 0;
  s_attrs[n++] = (Attribute){ .id = AttributeIdTitle, .cstring = s_title };
  if (s_subtitle[0]) {
    s_attrs[n++] = (Attribute){ .id = AttributeIdSubtitle, .cstring = s_subtitle };
  }
  s_attrs[n++] = (Attribute){ .id = AttributeIdBody, .cstring = s_body };

  memset(&s_item, 0, sizeof(s_item));
  s_item.header.timestamp = rtc_get_time();
  s_item.attr_list = (AttributeList){ .num_attributes = n, .attributes = s_attrs };

  s_info = (NotificationLayoutInfo){ .item = &s_item, .show_notification_timestamp = true };

  Window *window = window_create();
  if (!window) {
    return;
  }
  Layer *root = window_get_root_layer(window);
  GRect bounds;
  layer_get_bounds(root, &bounds);

  const LayoutLayerConfig config = {
    .frame = &bounds,
    .attributes = &s_item.attr_list,
    .mode = LayoutLayerModeCard,
    .app_id = &s_app_id,
    .context = &s_info,
  };
  LayoutLayer *layout = notification_layout_create(&config);
  if (layout) {
    layer_add_child(root, &layout->layer);
  }
  fw_window_stack_push(window);
  printk("NOTIF_SHOWN \"%s\"\n", s_title);
}
