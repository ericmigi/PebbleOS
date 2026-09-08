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
// SELECT opens a 1-item action menu ("Dismiss") that removes the notification
// from history; an arrival peek intro plays before the card.
// The card stays until BACK, the action-menu Dismiss, or the shipping
// notification-window timeout (alerts_preferences_get_notification_window_timeout_ms,
// 3 min default) — a per-session new_timer pops it back to the watchface.
// The timeout is refreshed on any interaction (UP/DOWN swap or SELECT), like
// shipping, so an actively-read notification does not vanish mid-read.
// ponytail: on dismiss the app returns without freeing the live swap layouts /
// window (a per-session leak); wire swap_layer_deinit + window free when the app
// gains a real teardown.

#include "applib/graphics/gtypes.h"
#include "applib/ui/layer.h"
#include "applib/ui/status_bar_layer.h"
#include "applib/ui/window.h"
#include "kernel/pbl_malloc.h"
#include "pbl/services/timeline/attribute.h"
#include "kernel/events.h"
#include "pbl/services/timeline/item.h"
#include "pbl/services/notifications/notification_storage.h"
#include "pbl/services/timeline/layout_layer.h"
#include "pbl/services/timeline/notification_layout.h"
#include "pbl/services/timeline/swap_layer.h"
#include "applib/ui/action_menu_window.h"
#include "applib/ui/action_menu_hierarchy.h"
#include "applib/ui/click.h"
#include "applib/ui/animation.h"
#include "applib/ui/property_animation.h"
#include "apps/system/timeline/peek_layer.h"
#include "pbl/services/evented_timer.h"
#include "pbl/services/new_timer/new_timer.h"
#include "pbl/services/timeline/timeline_resources.h"
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
extern void *layout_get_context(LayoutLayer *layout);
extern void fw_window_stack_pop(void);
extern void window_single_click_subscribe(ButtonId button_id, ClickHandler handler);
extern const LayoutColors *layout_get_notification_colors(const LayoutLayer *layout);
extern TimelineResourceId notification_layout_get_fallback_icon_id(uint8_t type);
extern int fw_window_stack_depth(void);
extern int fw_system_app_base_depth(void);
extern uint32_t alerts_preferences_get_notification_window_timeout_ms(void);
extern void event_put(PebbleEvent *event);

static void prv_refresh_notif_timeout(void);  // restarts the auto-dismiss timer

#define NOTIF_RING 8

typedef struct {
  char title[64];
  char subtitle[64];
  char body[128];
  uint32_t icon;
  Uuid id;  // storage id, so dismissing the card removes it from history
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

  // Persist to the real notification store (once per received notification) so
  // the launcher Notifications glance shows the last one and it survives a
  // reboot. notification_storage_store serializes a copy.
  Attribute attrs[3];
  uint8_t n = 0;
  attrs[n++] = (Attribute){ .id = AttributeIdTitle, .cstring = e->title };
  if (e->subtitle[0]) {
    attrs[n++] = (Attribute){ .id = AttributeIdSubtitle, .cstring = e->subtitle };
  }
  attrs[n++] = (Attribute){ .id = AttributeIdBody, .cstring = e->body };
  TimelineItem item = {0};
  item.header.type = TimelineItemTypeNotification;
  item.header.layout = LayoutIdNotification;
  item.header.timestamp = rtc_get_time();
  uuid_generate(&item.header.id);
  item.attr_list = (AttributeList){ .num_attributes = n, .attributes = attrs };
  e->id = item.header.id;  // remember it so Dismiss can remove it from history
  // Route through the real notification service: it stores the item and emits
  // PEBBLE_SYS_NOTIFICATION_EVENT(NotificationAdded) (which the launcher glance
  // is subscribed to), same as shipping.
  extern void notifications_add_notification(TimelineItem * notification);
  notifications_add_notification(&item);
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

// SELECT opens a 1-item action menu ("Dismiss"), mirroring notification_window.
static bool s_dismiss_pending;

static void prv_dismiss_action(ActionMenu *menu, const ActionMenuItem *action, void *ctx) {
  (void)menu; (void)action; (void)ctx;
  s_dismiss_pending = true;

  // Remove the dismissed notification from history so it no longer shows in the
  // Notifications app or the launcher glance, matching the shipping behaviour.
  const uint32_t idx = prv_current_index(&s_swap);
  NotifEntry *e = &s_ring[idx % NOTIF_RING];
  notification_storage_remove(&e->id);
  extern void event_put(PebbleEvent * event);
  PebbleEvent ev = {.type = PEBBLE_SYS_NOTIFICATION_EVENT};
  ev.sys_notification.type = NotificationRemoved;
  ev.sys_notification.notification_id = &e->id;
  event_put(&ev);
}

// Clear All: wipe the whole notification history (and the popup ring) and pop
// the card. Mirrors the shipping "Dismiss all" / Clear Notification History.
static void prv_clear_all_action(ActionMenu *menu, const ActionMenuItem *action, void *ctx) {
  (void)menu; (void)action; (void)ctx;
  extern void notification_storage_reset_and_init(void);
  s_dismiss_pending = true;
  notification_storage_reset_and_init();
  s_count = 0;  // drop the popup ring so a later swap shows nothing stale
  extern void event_put(PebbleEvent * event);
  PebbleEvent ev = {.type = PEBBLE_SYS_NOTIFICATION_EVENT};
  ev.sys_notification.type = NotificationRemoved;
  event_put(&ev);
}

static void prv_menu_did_close(ActionMenu *menu, const ActionMenuItem *performed, void *ctx) {
  (void)menu; (void)performed; (void)ctx;
  if (s_dismiss_pending) {
    s_dismiss_pending = false;
    fw_window_stack_pop();  // menu closed -> notification is top; pop it to dismiss
  }
}

// Any interaction (UP/DOWN swap via the swap_layer, or SELECT) pushes the
// auto-dismiss deadline out, like shipping's notification_window.
static void prv_on_interaction(SwapLayer *sl, void *ctx) {
  (void)sl; (void)ctx;
  prv_refresh_notif_timeout();
}

static void prv_select_click(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer; (void)context;
  prv_refresh_notif_timeout();
  ActionMenuLevel *root = action_menu_level_create(2);
  if (!root) {
    return;
  }
  action_menu_level_add_action(root, "Dismiss", prv_dismiss_action, NULL);
  action_menu_level_add_action(root, "Clear All", prv_clear_all_action, NULL);
  ActionMenuConfig config = {
    .root_level = root,
    .colors = { .background = GColorDarkGray, .foreground = GColorWhite },
    .did_close = prv_menu_did_close,
  };
  app_action_menu_open(&config);  // ponytail: root_level leaked per open
}

static void prv_notif_click_config(void *context) {
  (void)context;
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click);
}

// Arrival peek intro: the notification icon appears full-window, "unfolds"
// (peek_layer_play), then slides up to reveal the card. Minimal vs
// notification_window (no swap-frame slide / scale-to-image / moook interp).
static PeekLayer *s_peek;

static void prv_peek_anim_stopped(Animation *animation, bool finished, void *context) {
  (void)animation; (void)finished; (void)context;
  if (s_peek) {
    peek_layer_destroy(s_peek);
    s_peek = NULL;
  }
}

static void prv_hide_peek(void *data) {
  (void)data;
  if (!s_peek) {
    return;
  }
  Layer *pl = (Layer *)s_peek;
  const GRect start = pl->frame;
  GRect stop = start;
  stop.origin.y -= stop.size.h;  // slide up off-screen to reveal the card
  PropertyAnimation *pa = property_animation_create_layer_frame(pl, &start, &stop);
  if (!pa) {
    prv_peek_anim_stopped(NULL, false, NULL);
    return;
  }
  Animation *anim = property_animation_get_animation(pa);
  animation_set_duration(anim, 300);
  animation_set_handlers(anim, (AnimationHandlers){ .stopped = prv_peek_anim_stopped }, NULL);
  animation_schedule(anim);
}

static void prv_play_peek(void *data) {
  (void)data;
  if (!s_peek) {
    return;
  }
  peek_layer_play(s_peek);
  evented_timer_register(500, false, prv_hide_peek, NULL);
}

static void prv_start_peek(Layer *root, LayoutLayer *current) {
  if (!current) {
    return;
  }
  s_peek = peek_layer_create(s_win_bounds);
  if (!s_peek) {
    return;
  }
  TimelineItem *item = layout_get_context(current);  // bundle item
  const TimelineResourceId fallback =
      notification_layout_get_fallback_icon_id(TimelineItemTypeNotification);
  const TimelineResourceInfo res = {
    .res_id = attribute_get_uint32(&item->attr_list, AttributeIdIconTiny, fallback),
    .app_id = &s_app_id,
    .fallback_id = fallback,
  };
  peek_layer_set_icon(s_peek, &res);
  peek_layer_set_background_color(s_peek, layout_get_notification_colors(current)->bg_color);
  layer_add_child(root, (Layer *)s_peek);  // on top, covers the card
  evented_timer_register(100, false, prv_play_peek, NULL);
}

// Auto-dismiss timeout: like shipping's notification_window, the card retires to
// whatever is underneath after a few minutes with no interaction. Uses new_timer
// (the port pumps it); its callback runs on a timer thread, so it posts a
// KernelMain callback that pops the notification app's windows.
static TimerID s_pop_timer = TIMER_INVALID_ID;
static int s_notif_base_depth;
static bool s_notif_up;

static void prv_pop_notif_cb(void *data) {
  (void)data;
  if (!s_notif_up) {
    return;
  }
  // Pop the notif app's window(s) — the card and any action menu on top of it —
  // back down to its launch base, so app_event_loop returns and the app exits.
  while (fw_window_stack_depth() > s_notif_base_depth) {
    fw_window_stack_pop();
  }
}

static void prv_pop_timer_fired(void *data) {
  (void)data;
  PebbleEvent e = {
    .type = PEBBLE_CALLBACK_EVENT,
    .callback = { .callback = prv_pop_notif_cb },
  };
  event_put(&e);
}

static void prv_refresh_notif_timeout(void) {
  if (s_notif_up && s_pop_timer != TIMER_INVALID_ID) {
    new_timer_start(s_pop_timer, alerts_preferences_get_notification_window_timeout_ms(),
                    prv_pop_timer_fired, NULL, 0);
  }
}

// main_func for the notification system-app: builds the swap_layer-hosted card
// and pumps app_event_loop until BACK pops it (or the timeout fires).
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
    .interaction_handler = prv_on_interaction,
    .click_config_provider = prv_notif_click_config,
  });
  swap_layer_set_click_config_onto_window(&s_swap, window);
  layer_add_child(root, swap_layer_get_layer(&s_swap));
  layer_add_child(root, &s_status.layer);  // status bar on top
  swap_layer_reload_data(&s_swap);

  prv_start_peek(root, swap_layer_get_current_layout(&s_swap));

  app_window_stack_push(window, false /* animated */);
  printk("NOTIF_SHOWN \"%s\"\n", s_ring[(s_count ? s_count - 1 : 0) % NOTIF_RING].title);

  // Arm the auto-dismiss timeout for this popup session.
  s_notif_base_depth = fw_system_app_base_depth();
  s_notif_up = true;
  if (s_pop_timer == TIMER_INVALID_ID) {
    s_pop_timer = new_timer_create();
  }
  new_timer_start(s_pop_timer, alerts_preferences_get_notification_window_timeout_ms(),
                  prv_pop_timer_fired, NULL, 0);

  app_event_loop();

  s_notif_up = false;
  new_timer_stop(s_pop_timer);
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

  // Quiet Time / Do Not Disturb: the notification is already stored to history
  // and the glance was refreshed (fw_notification_show); suppress only the popup.
  extern bool do_not_disturb_is_active(void);
  if (do_not_disturb_is_active()) {
    return;
  }

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
