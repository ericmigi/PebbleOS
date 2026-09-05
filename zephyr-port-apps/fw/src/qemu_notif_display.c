/* SPDX-License-Identifier: Apache-2.0 */

// Notification display on the Zephyr port. qemu_notif_rx.c / qemu_ancs.c decode
// a notification and call fw_notification_show (ring append + flag).
// fw_notification_poll runs on KernelMain from the shared UI pump and launches a
// notification system-app (prv_notif_app_main), which hosts the shipping
// timeline notification_layout card inside a swap_layer — same structure as
// notification_window: card offset below the status bar, status-bar colour
// coordinated with the band, UP/DOWN swap between notifications.
//
// Multiple notifications: fw_notification_show appends to a ring; each visible
// card carries its absolute ring index in its heap context bundle, so
// get_layout(rel) resolves relative to the current card and UP/DOWN swap to the
// older/newer notification. A notification arriving while the app is already up
// reloads the swap_layer to focus the newest.
//
// ponytail: on dismiss (BACK) the app returns without freeing the live swap
// layouts / window (a per-session leak); wire swap_layer_deinit + window free
// when the app gains a real teardown. No peek intro animation / action menu yet.

#include "applib/graphics/gtypes.h"
#include "applib/ui/layer.h"
#include "applib/ui/status_bar_layer.h"
#include "applib/ui/window.h"
#include "kernel/pbl_malloc.h"
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
extern void fw_system_app_launch(const PebbleProcessMd *md);
extern void layout_destroy(LayoutLayer *layout);

#define NOTIF_RING 8

typedef struct {
  char title[64];
  char subtitle[64];
  char body[128];
  uint32_t icon;
} NotifEntry;

static NotifEntry s_ring[NOTIF_RING];
static volatile uint32_t s_count;  // monotonic total received (single writer: RX thread)
static volatile bool s_pending;
static bool s_app_running;

static Uuid s_app_id;  // zeroed = invalid: no phone app icon in the port
static StatusBarLayer s_status;
static SwapLayer s_swap;
static GRect s_win_bounds;

// Per-visible-card heap bundle. `item` is first so layout_get_context (which
// returns notification_layout's info.item) yields the bundle pointer; freed in
// the swap_layer layout_removed callback.
typedef struct {
  TimelineItem item;
  uint32_t abs_index;
  uint32_t icon;
  NotificationLayoutInfo info;
  Attribute attrs[4];
  char title[64];
  char subtitle[64];
  char body[128];
} NotifLayoutCtx;

void fw_notification_show(const char *title, const char *subtitle, const char *body,
                          uint32_t icon) {
  NotifEntry *e = &s_ring[s_count % NOTIF_RING];
  strncpy(e->title, title ? title : "", sizeof(e->title) - 1);
  e->title[sizeof(e->title) - 1] = '\0';
  strncpy(e->subtitle, subtitle ? subtitle : "", sizeof(e->subtitle) - 1);
  e->subtitle[sizeof(e->subtitle) - 1] = '\0';
  strncpy(e->body, body ? body : "", sizeof(e->body) - 1);
  e->body[sizeof(e->body) - 1] = '\0';
  e->icon = icon;
  s_count++;
  s_pending = true;
}

// Absolute ring index of the swap_layer's current card (newest when none yet).
static uint32_t prv_current_index(SwapLayer *sl) {
  LayoutLayer *cur = swap_layer_get_current_layout(sl);
  if (!cur) {
    return s_count ? s_count - 1 : 0;
  }
  NotifLayoutCtx *c = (NotifLayoutCtx *)layout_get_context(cur);  // item is first member
  return c->abs_index;
}

static LayoutLayer *prv_get_layout(SwapLayer *sl, int8_t rel_position, void *ctx) {
  (void)ctx;
  const uint32_t count = s_count;
  const int64_t base = (int64_t)prv_current_index(sl);
  const int64_t abs = base + rel_position;
  if (abs < 0 || (uint64_t)abs >= count) {
    return NULL;
  }
  if (count > NOTIF_RING && (uint64_t)abs < (uint64_t)count - NOTIF_RING) {
    return NULL;  // evicted from the ring
  }

  const NotifEntry *e = &s_ring[abs % NOTIF_RING];
  NotifLayoutCtx *c = kernel_malloc(sizeof(*c));
  if (!c) {
    return NULL;
  }
  memset(c, 0, sizeof(*c));
  c->abs_index = (uint32_t)abs;
  strncpy(c->title, e->title, sizeof(c->title) - 1);
  strncpy(c->subtitle, e->subtitle, sizeof(c->subtitle) - 1);
  strncpy(c->body, e->body, sizeof(c->body) - 1);
  c->icon = e->icon;

  uint8_t n = 0;
  c->attrs[n++] = (Attribute){ .id = AttributeIdTitle, .cstring = c->title };
  if (c->subtitle[0]) {
    c->attrs[n++] = (Attribute){ .id = AttributeIdSubtitle, .cstring = c->subtitle };
  }
  c->attrs[n++] = (Attribute){ .id = AttributeIdBody, .cstring = c->body };
  if (c->icon) {
    c->attrs[n++] = (Attribute){ .id = AttributeIdIconTiny, .uint32 = c->icon };
  }

  c->item.header.type = TimelineItemTypeNotification;
  c->item.header.timestamp = rtc_get_time();
  c->item.attr_list = (AttributeList){ .num_attributes = n, .attributes = c->attrs };
  c->info = (NotificationLayoutInfo){ .item = &c->item, .show_notification_timestamp = true };

  const LayoutLayerConfig config = {
    .frame = &s_win_bounds,
    .attributes = &c->item.attr_list,
    .mode = LayoutLayerModeCard,
    .app_id = &s_app_id,
    .context = &c->info,
  };
  return notification_layout_create(&config);
}

static void prv_layout_removed(SwapLayer *sl, LayoutLayer *layout, void *ctx) {
  (void)sl; (void)ctx;
  void *bundle = layout_get_context(layout);  // == &ctx->item == bundle (item first)
  layout_destroy(layout);
  kernel_free(bundle);
}

// Coordinate the status-bar clock colour with the card: fill only the status-bar
// strip with the band colour (the layout paints just the banner + text, leaving
// the body to the window background, so filling the window would paint the body).
static void prv_update_colors(SwapLayer *sl, GColor bg_color, bool status_bar_filled, void *ctx) {
  (void)sl; (void)ctx;
  const GColor status_color = status_bar_filled ? bg_color : GColorWhite;
  status_bar_layer_set_colors(&s_status, status_color, gcolor_legible_over(status_color));
}

// main_func for the notification system-app: builds the swap_layer-hosted card
// and pumps app_event_loop until BACK pops it.
static void prv_notif_app_main(void) {
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

  const GRect swap_frame = GRect(0, status_bar_height, s_win_bounds.size.w,
                                 s_win_bounds.size.h - status_bar_height);
  swap_layer_init(&s_swap, &swap_frame);
  swap_layer_set_callbacks(&s_swap, NULL, (SwapLayerCallbacks){
    .get_layout_handler = prv_get_layout,
    .layout_removed_handler = prv_layout_removed,
    .update_colors_handler = prv_update_colors,
  });
  swap_layer_set_click_config_onto_window(&s_swap, window);
  layer_add_child(root, swap_layer_get_layer(&s_swap));
  layer_add_child(root, &s_status.layer);  // status bar on top
  swap_layer_reload_data(&s_swap);

  app_window_stack_push(window, false /* animated */);
  printk("NOTIF_SHOWN \"%s\"\n", s_ring[(s_count ? s_count - 1 : 0) % NOTIF_RING].title);
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

// Called from fw_ui_pump_once (KernelMain). Launches the notification app, or
// reloads the swap_layer to focus the newest if the app is already up.
void fw_notification_poll(void) {
  if (!s_pending) {
    return;
  }
  s_pending = false;

  if (s_app_running) {
    // A new notification arrived while the card is up: refetch so the newest
    // becomes current (reload nulls current -> get_layout(0) returns newest).
    swap_layer_reload_data(&s_swap);
    return;
  }

  // Launch inline (mirrors the pump's own s_pending_md processing); the pump's
  // event_take_timeout returns early on its 1s idle timeout before draining a
  // queued launch. No fw_shell_on_app_exit: a dismissed notification returns to
  // whatever was underneath, and fw_system_app_launch returns to the calling
  // pump which re-renders it.
  s_app_running = true;
  fw_system_app_launch((const PebbleProcessMd *)&s_notif_md);
  s_app_running = false;
}
