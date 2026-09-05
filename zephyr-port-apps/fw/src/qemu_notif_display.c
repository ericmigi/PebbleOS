/* SPDX-License-Identifier: Apache-2.0 */

// Notification display on the Zephyr port. qemu_notif_rx.c / qemu_ancs.c decode
// a notification and call fw_notification_show (thread-safe stash + flag).
// fw_notification_poll runs on KernelMain from the shared UI pump and requests a
// privileged system-app launch; prv_notif_app_main then builds the real timeline
// notification_layout card and runs the standard app_event_loop.
//
// Launching through fw_shell_request_launch + app_event_loop (the same path the
// launcher uses for Settings/Music) gives a clean push/pop lifecycle: BACK pops
// the card and app_event_loop returns, restoring the launcher/watchface intact.
// Pushing a raw window from inside the pump corrupted the underlying app's loop
// (it kept rendering the popped card and stopped taking input).
//
// The card pixels come from the shipping timeline notification_layout (title,
// body, relative timestamp, colour band) plus a clock status bar, near
// pixel-identical to the FreeRTOS reference.
//
// ponytail: the dismissed window + layout are not freed (one static show at a
// time; a per-notification leak until multi-notification nav is wired). No peek
// intro animation / scroll / action menu — that chrome is the swap_layer +
// modal_manager epic. The card body itself matches the reference.

#include "applib/graphics/gtypes.h"
#include "applib/ui/layer.h"
#include "applib/ui/status_bar_layer.h"
#include "applib/ui/window.h"
#include "pbl/services/timeline/attribute.h"
#include "pbl/services/timeline/item.h"
#include "pbl/services/timeline/layout_layer.h"
#include "pbl/services/timeline/notification_layout.h"
#include "pbl/services/timeline/swap_layer.h"
#include "process_management/pebble_process_md.h"
#include "util/uuid.h"

#include <zephyr/sys/printk.h>
#include <string.h>
#include <time.h>

extern time_t rtc_get_time(void);
extern Window *window_create(void);
extern void app_window_stack_push(Window *window, bool animated);
extern void app_event_loop(void);
extern void layout_destroy(LayoutLayer *layout);
extern void fw_system_app_launch(const PebbleProcessMd *md);

static char s_title[64];
static char s_subtitle[64];
static char s_body[128];
static volatile bool s_pending;

static Attribute s_attrs[3];
static TimelineItem s_item;
static NotificationLayoutInfo s_info;
static Uuid s_app_id;  // zeroed = invalid: no phone app icon in the port
static StatusBarLayer s_status;
static SwapLayer s_swap;
static GRect s_win_bounds;

void fw_notification_show(const char *title, const char *subtitle, const char *body) {
  strncpy(s_title, title ? title : "", sizeof(s_title) - 1);
  s_title[sizeof(s_title) - 1] = '\0';
  strncpy(s_subtitle, subtitle ? subtitle : "", sizeof(s_subtitle) - 1);
  s_subtitle[sizeof(s_subtitle) - 1] = '\0';
  strncpy(s_body, body ? body : "", sizeof(s_body) - 1);
  s_body[sizeof(s_body) - 1] = '\0';
  s_pending = true;
}

// swap_layer fetches the notification card for the focused position; single
// notification, so only rel_position 0 has a layout.
static LayoutLayer *prv_get_layout(SwapLayer *sl, int8_t rel_position, void *ctx) {
  (void)sl; (void)ctx;
  if (rel_position != 0) {
    return NULL;
  }
  const LayoutLayerConfig config = {
    .frame = &s_win_bounds,
    .attributes = &s_item.attr_list,
    .mode = LayoutLayerModeCard,
    .app_id = &s_app_id,
    .context = &s_info,
  };
  return notification_layout_create(&config);
}

static void prv_layout_removed(SwapLayer *sl, LayoutLayer *layout, void *ctx) {
  (void)sl; (void)ctx;
  layout_destroy(layout);  // s_item is static, so no timeline_item_destroy
}

// main_func for the notification system-app: builds the card, pushes it via the
// app window stack, and pumps app_event_loop until BACK pops it.
static void prv_notif_app_main(void) {
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
  layer_get_bounds(root, &s_win_bounds);

  status_bar_layer_init(&s_status);
  status_bar_layer_set_colors(&s_status, GColorClear, GColorWhite);
  status_bar_layer_set_separator_mode(&s_status, StatusBarLayerSeparatorModeNone);
  const int16_t status_bar_height = s_status.layer.frame.size.h;

  // Host the card in a swap_layer offset below the status bar (mirrors
  // notification_window): the layout's colour band + icon then sit under the
  // clock instead of colliding with it, and UP/DOWN scroll the card.
  const GRect swap_frame = GRect(0, status_bar_height, s_win_bounds.size.w,
                                 s_win_bounds.size.h - status_bar_height);
  swap_layer_init(&s_swap, &swap_frame);
  swap_layer_set_callbacks(&s_swap, NULL, (SwapLayerCallbacks){
    .get_layout_handler = prv_get_layout,
    .layout_removed_handler = prv_layout_removed,
  });
  swap_layer_set_click_config_onto_window(&s_swap, window);
  layer_add_child(root, swap_layer_get_layer(&s_swap));
  layer_add_child(root, &s_status.layer);  // status bar on top
  swap_layer_reload_data(&s_swap);

  app_window_stack_push(window, false /* animated */);
  printk("NOTIF_SHOWN \"%s\"\n", s_title);
  app_event_loop();
}

static const PebbleProcessMdSystem s_notif_md = {
  .common = {
    .main_func = prv_notif_app_main,
    .process_storage = ProcessStorageBuiltin,
    // UUID: e7c9f1a2-0000-4000-8000-6e6f74696600 ("notif")
    .uuid = {0xe7, 0xc9, 0xf1, 0xa2, 0x00, 0x00, 0x40, 0x00,
             0x80, 0x00, 0x6e, 0x6f, 0x74, 0x69, 0x66, 0x00},
  },
  .name = "Notification",
};

// Called from fw_ui_pump_once (KernelMain). Requests the notification app launch.
void fw_notification_poll(void) {
  if (!s_pending) {
    return;
  }
  s_pending = false;
  // Launch inline (mirrors the pump's own s_pending_md processing). Not via
  // fw_shell_request_launch: the pump's event_take_timeout returns early on a 1s
  // idle timeout, before it would drain a queued launch, so a queued
  // notification would not appear until the next button event.
  //
  // No fw_shell_on_app_exit: that force-chains the launcher on app exit, but a
  // dismissed notification must return to whatever was underneath (watchface or
  // launcher). fw_system_app_launch returns to the calling pump, which re-renders
  // the underlying screen on its own.
  fw_system_app_launch((const PebbleProcessMd *)&s_notif_md);
}
