/* SPDX-License-Identifier: Apache-2.0 */

// Backing-service ceiling for the real Notifications app/window/layout stack.
//
// ponytail: notification history is one immutable RAM canned item; there is no
// blob_db/pin_db persistence, phone sync, app-resource lookup, or notification
// image fetch. Upgrade by replacing this file with notification_storage.c,
// pin_db.c/timeline_item_storage, timeline_resources.c, and the phone imaging
// endpoint as those services become available in the Zephyr scaffold. The UI
// sources themselves remain the real PebbleOS implementations.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "applib/graphics/gtypes.h"
#include "applib/graphics/gdraw_command_image.h"
#include "applib/ui/action_menu_window.h"
#include "applib/ui/action_menu_hierarchy.h"
#include "applib/ui/app_window_stack.h"
#include "applib/ui/click_internal.h"
#include "applib/ui/kino/kino_layer.h"
#include "applib/ui/vibes.h"
#include "applib/ui/window_private.h"
#include "applib/ui/window_manager.h"
#include "applib/ui/window_stack.h"
#include "apps/system/timeline/peek_layer.h"
#include "kernel/ui/modals/modal_manager.h"
#include "kernel/pbl_malloc.h"
#include "pbl/services/bluetooth/bluetooth_persistent_storage.h"
#include "pbl/services/blob_db/ios_notif_pref_db.h"
#include "pbl/services/blob_db/pin_db.h"
#include "pbl/services/blob_db/reminder_db.h"
#include "pbl/services/comm_session/session.h"
#include "pbl/services/light.h"
#include "pbl/services/notifications/alerts_preferences.h"
#include "pbl/services/notifications/alerts_preferences_private.h"
#include "pbl/services/notifications/alerts_private.h"
#include "pbl/services/notifications/ancs/ancs_filtering.h"
#include "pbl/services/notifications/do_not_disturb.h"
#include "pbl/services/notifications/notification_image.h"
#include "pbl/services/notifications/notification_storage.h"
#include "pbl/services/timeline/attribute.h"
#include "resource/timeline_resource_ids.auto.h"
#include "pbl/services/timeline/notification_layout.h"
#include "pbl/services/timeline/layout_layer.h"
#include "pbl/services/timeline/timeline_layout.h"
#include "pbl/services/timeline/timeline_resources.h"
#include "pbl/services/timeline/reminders.h"
#include "pbl/services/timeline/timeline.h"
#include "pbl/services/timeline/timeline_actions.h"
#include "resource/resource_ids.auto.h"
#include "shell/system_theme.h"
#include "system/status_codes.h"
#include "launcher_ui.h"

static const Uuid s_notification_id = {
  0x50, 0x65, 0x62, 0x62, 0x6c, 0x65, 0x4f, 0x53,
  0x8a, 0x70, 0x4e, 0x6f, 0x74, 0x69, 0x66, 0x21,
};

static char s_app_name[] = "PebbleOS";
static char s_title[] = "Notifications are ready";
static char s_body[] = "This card is rendered by the real Pebble notification layout.";
static Attribute s_attributes[] = {
  { .id = AttributeIdAppName, .cstring = s_app_name },
  { .id = AttributeIdTitle, .cstring = s_title },
  { .id = AttributeIdBody, .cstring = s_body },
  { .id = AttributeIdIconTiny, .uint32 = TIMELINE_RESOURCE_NOTIFICATION_GENERIC },
};

static TimelineItem prv_canned_item(void) {
  return (TimelineItem) {
    .header = {
      .id = s_notification_id,
      .timestamp = 1787587200,
      .type = TimelineItemTypeNotification,
      .visible = true,
      .layout = LayoutIdNotification,
    },
    .attr_list = {
      .num_attributes = sizeof(s_attributes) / sizeof(s_attributes[0]),
      .attributes = s_attributes,
    },
  };
}

void notification_storage_init(void) {}
void notification_storage_lock(void) {}
void notification_storage_unlock(void) {}
void notification_storage_store(TimelineItem *notification) { (void)notification; }
bool notification_storage_notification_exists(const Uuid *id) {
  return id && uuid_equal(id, &s_notification_id);
}
size_t notification_storage_get_len(const Uuid *id) {
  return notification_storage_notification_exists(id) ? sizeof(TimelineItem) : 0;
}
bool notification_storage_get(const Uuid *id, TimelineItem *item_out) {
  if (!item_out || !notification_storage_notification_exists(id)) {
    return false;
  }
  *item_out = prv_canned_item();
  return true;
}
void notification_storage_set_status(const Uuid *id, uint8_t status) {
  (void)id;
  (void)status;
}
bool notification_storage_get_status(const Uuid *id, uint8_t *status) {
  if (!notification_storage_notification_exists(id)) {
    return false;
  }
  if (status) {
    *status = 0;
  }
  return true;
}
void notification_storage_remove(const Uuid *id) { (void)id; }
bool notification_storage_find_ancs_notification_id(uint32_t ancs_uid, Uuid *uuid_out) {
  (void)ancs_uid;
  (void)uuid_out;
  return false;
}
bool notification_storage_find_ancs_notification_by_timestamp(
    TimelineItem *notification, CommonTimelineItemHeader *header_out) {
  (void)notification;
  (void)header_out;
  return false;
}
void notification_storage_iterate(
    bool (*iter_callback)(void *data, SerializedTimelineItemHeader *header), void *data) {
  if (!iter_callback) {
    return;
  }
  SerializedTimelineItemHeader header = {
    .common = {
      .id = s_notification_id,
      .timestamp = 1787587200,
      .type = TimelineItemTypeNotification,
      .visible = true,
      .layout = LayoutIdNotification,
    },
    .num_attributes = sizeof(s_attributes) / sizeof(s_attributes[0]),
  };
  iter_callback(data, &header);
}
void notification_storage_rewrite(
    void (*iter_callback)(TimelineItem *notification,
                          SerializedTimelineItemHeader *header, void *data), void *data) {
  (void)iter_callback;
  (void)data;
}
void notification_storage_reset_and_init(void) {}

status_t pin_db_get(const TimelineItemId *id, TimelineItem *pin) {
  (void)id;
  (void)pin;
  return -1;
}
status_t pin_db_read_item_header(TimelineItem *item_out, TimelineItemId *id) {
  (void)item_out;
  (void)id;
  return -1;
}

bool timeline_resources_is_system(TimelineResourceId timeline_id) {
  return (timeline_id & SYSTEM_RESOURCE_FLAG) != 0;
}
void timeline_resources_get_id(const TimelineResourceInfo *timeline_res,
                               TimelineResourceSize size, AppResourceInfo *res_info_out) {
  (void)timeline_res;
  (void)size;
  *res_info_out = (AppResourceInfo) { .res_app_num = SYSTEM_APP, .res_id = 0 };
}

bool alerts_preferences_get_notification_alternative_design(void) { return false; }
NotificationStatusBarStyle alerts_preferences_get_notification_status_bar_style(void) {
  return NotificationStatusBarStyle_Default;
}

const GBitmap *notification_image_lock(const Uuid *item_id) {
  (void)item_id;
  return NULL;
}
void notification_image_unlock(void) {}
bool notification_image_is_pending(const Uuid *item_id) {
  (void)item_id;
  return false;
}

const char *system_theme_get_font_key(TextStyleFont font) {
  (void)font;
  return FONT_KEY_GOTHIC_14;
}
const char *system_theme_get_font_key_for_size(PreferredContentSize size, TextStyleFont font) {
  (void)size;
  return system_theme_get_font_key(font);
}
PreferredContentSize system_theme_get_content_size(void) {
  return PreferredContentSizeLarge;
}

GDrawCommandImage *gdraw_command_image_create_with_resource_system(ResAppNum app_num,
                                                                   uint32_t resource_id) {
  (void)app_num;
  (void)resource_id;
  return NULL;
}
void gdraw_command_image_destroy(GDrawCommandImage *image) { (void)image; }
GSize gdraw_command_image_get_bounds_size(GDrawCommandImage *image) {
  (void)image;
  return GSizeZero;
}
void gdraw_command_image_draw(GContext *ctx, GDrawCommandImage *image, GPoint offset) {
  (void)ctx;
  (void)image;
  (void)offset;
}

void itoa_int(int value, char *buffer, int base) {
  char reversed[34];
  unsigned int magnitude;
  unsigned int count = 0;
  bool negative = value < 0;

  if (!buffer || base < 2 || base > 10) {
    return;
  }
  magnitude = negative ? (unsigned int)(-(value + 1)) + 1u : (unsigned int)value;
  do {
    reversed[count++] = (char)('0' + magnitude % (unsigned int)base);
    magnitude /= (unsigned int)base;
  } while (magnitude);
  if (negative) {
    reversed[count++] = '-';
  }
  for (unsigned int i = 0; i < count; ++i) {
    buffer[i] = reversed[count - i - 1];
  }
  buffer[count] = '\0';
}

const char *string_strip_leading_whitespace(const char *string) {
  while (*string == ' ' || *string == '\n') {
    ++string;
  }
  return string;
}

typedef struct {
  KinoReel base;
} NotificationIconReel;

KinoReel *kino_reel_create_with_resource_system(ResAppNum app_num, uint32_t resource_id) {
  (void)app_num;
  (void)resource_id;
  return task_zalloc(sizeof(NotificationIconReel));
}

void kino_reel_destroy(KinoReel *reel) {
  task_free(reel);
}

GSize kino_reel_get_size(KinoReel *reel) {
  return reel ? GSize(25, 25) : GSizeZero;
}

void kino_layer_init(KinoLayer *kino_layer, const GRect *frame) {
  *kino_layer = (KinoLayer) {};
  layer_init(&kino_layer->layer, frame);
  kino_layer->alignment = GAlignCenter;
}

void kino_layer_deinit(KinoLayer *kino_layer) {
  if (kino_layer->player.reel && kino_layer->player.owns_reel) {
    kino_reel_destroy(kino_layer->player.reel);
  }
  layer_deinit(&kino_layer->layer);
}

KinoLayer *kino_layer_create(GRect frame) {
  KinoLayer *kino_layer = task_zalloc(sizeof(*kino_layer));
  if (kino_layer) {
    kino_layer_init(kino_layer, &frame);
  }
  return kino_layer;
}

void kino_layer_destroy(KinoLayer *kino_layer) {
  if (kino_layer) {
    kino_layer_deinit(kino_layer);
    task_free(kino_layer);
  }
}

Layer *kino_layer_get_layer(KinoLayer *kino_layer) {
  return kino_layer ? &kino_layer->layer : NULL;
}

void kino_layer_set_reel(KinoLayer *kino_layer, KinoReel *reel, bool take_ownership) {
  kino_layer->player.reel = reel;
  kino_layer->player.owns_reel = take_ownership;
}

void kino_layer_set_reel_with_resource_system(KinoLayer *kino_layer, ResAppNum app_num,
                                              uint32_t resource_id, bool invert) {
  (void)invert;
  kino_layer_set_reel(kino_layer, kino_reel_create_with_resource_system(app_num, resource_id),
                      true);
}

void kino_layer_set_alignment(KinoLayer *kino_layer, GAlign alignment) {
  kino_layer->alignment = alignment;
}

GAlign kino_layer_get_alignment(KinoLayer *kino_layer) {
  return kino_layer->alignment;
}

void kino_layer_play(KinoLayer *kino_layer) {
  (void)kino_layer;
}

LayoutLayer *generic_layout_create(const LayoutLayerConfig *config) {
  (void)config;
  return NULL;
}
bool generic_layout_verify(bool existing_attributes[]) {
  (void)existing_attributes;
  return false;
}
LayoutLayer *calendar_layout_create(const LayoutLayerConfig *config) {
  (void)config;
  return NULL;
}
bool calendar_layout_verify(bool existing_attributes[]) {
  (void)existing_attributes;
  return false;
}
LayoutLayer *alarm_layout_create(const LayoutLayerConfig *config) {
  (void)config;
  return NULL;
}
bool alarm_layout_verify(bool existing_attributes[]) {
  (void)existing_attributes;
  return false;
}
LayoutLayer *health_layout_create(const LayoutLayerConfig *config) {
  (void)config;
  return NULL;
}
bool health_layout_verify(bool existing_attributes[]) {
  (void)existing_attributes;
  return false;
}
LayoutLayer *sports_layout_create(const LayoutLayerConfig *config) {
  (void)config;
  return NULL;
}
bool sports_layout_verify(bool existing_attributes[]) {
  (void)existing_attributes;
  return false;
}
LayoutLayer *weather_layout_create(const LayoutLayerConfig *config) {
  (void)config;
  return NULL;
}
bool weather_layout_verify(bool existing_attributes[]) {
  (void)existing_attributes;
  return false;
}

GTextNodeCustom *timeline_layout_create_icon_node(const TimelineLayout *layout) {
  return layout_node_create_kino_layer_wrapper((KinoLayer *)&layout->icon_layer);
}

static void prv_empty_page_break_node(GContext *ctx, const GRect *box,
                                      const GTextNodeDrawConfig *config, bool render,
                                      GSize *size_out, void *user_data) {
  (void)ctx;
  (void)box;
  (void)config;
  (void)render;
  (void)user_data;
  if (size_out) {
    *size_out = GSizeZero;
  }
}

GTextNodeCustom *timeline_layout_create_page_break_node(const TimelineLayout *layout) {
  (void)layout;
  return graphics_text_node_create_custom(prv_empty_page_break_node, NULL);
}

void notification_image_service_init(void) {}

bool notification_image_claim(const Uuid *item_id, uint8_t *token_out) {
  (void)item_id;
  (void)token_out;
  return false;
}

bool notification_image_store(uint8_t token, GBitmap *bitmap) {
  (void)token;
  (void)bitmap;
  return false;
}

void notification_image_clear(void) {}

bool do_not_disturb_is_active(void) { return false; }
void do_not_disturb_toggle_manually_enabled(ManualDNDFirstUseSource source) { (void)source; }
void do_not_disturb_manual_toggle_with_dialog(void) {}
bool alerts_preferences_dnd_get_auto_dismiss(void) { return false; }
uint32_t alerts_get_notification_window_timeout_ms(void) { return 10000; }

void timeline_actions_dismiss_all(NotificationInfo *notifications, int count,
                                  ActionMenu *action_menu, ActionCompleteCallback callback,
                                  void *callback_data) {
  (void)notifications;
  (void)count;
  (void)action_menu;
  if (callback) {
    callback(true, callback_data);
  }
}

ActionMenuLevel *timeline_actions_create_action_menu_root_level(
    uint8_t num_actions, uint8_t separator_index, TimelineItemActionSource source) {
  (void)num_actions;
  (void)separator_index;
  (void)source;
  return NULL;
}

void timeline_actions_add_action_to_root_level(TimelineItemAction *action,
                                               ActionMenuLevel *root_level) {
  (void)action;
  (void)root_level;
}

ActionMenu *timeline_actions_push_action_menu(ActionMenuConfig *config,
                                              WindowStack *window_stack) {
  (void)config;
  (void)window_stack;
  return NULL;
}

void timeline_invoke_action(const TimelineItem *item, const TimelineItemAction *action,
                            const AttributeList *attributes) {
  (void)item;
  (void)action;
  (void)attributes;
}

bool timeline_get_originator_id(const TimelineItem *item, Uuid *id) {
  (void)item;
  if (id) {
    *id = (Uuid)UUID_INVALID;
  }
  return false;
}

status_t reminder_db_read_item(TimelineItem *item_out, TimelineItemId *id) {
  (void)item_out;
  (void)id;
  return -1;
}

status_t reminder_db_delete_item(const TimelineItemId *id, bool send_event) {
  (void)id;
  (void)send_event;
  return -1;
}

bool reminders_can_snooze(Reminder *reminder) {
  (void)reminder;
  return false;
}

status_t reminders_snooze(Reminder *reminder) {
  (void)reminder;
  return -1;
}

CommSession *comm_session_get_system_session(void) { return NULL; }

bool comm_session_has_capability(CommSession *session, CommSessionCapability capability) {
  (void)session;
  (void)capability;
  return false;
}

void bt_persistent_storage_get_cached_system_capabilities(
    PebbleProtocolCapabilities *capabilities_out) {
  if (capabilities_out) {
    *capabilities_out = (PebbleProtocolCapabilities) {};
  }
}

iOSNotifPrefs *ios_notif_pref_db_get_prefs(const uint8_t *app_id, int length) {
  (void)app_id;
  (void)length;
  return NULL;
}

void ios_notif_pref_db_free_prefs(iOSNotifPrefs *prefs) { (void)prefs; }

status_t ios_notif_pref_db_store_prefs(const uint8_t *app_id, int length,
                                       AttributeList *attributes,
                                       TimelineItemActionGroup *action_group) {
  (void)app_id;
  (void)length;
  (void)attributes;
  (void)action_group;
  return -1;
}

uint8_t ancs_filtering_get_mute_type(const iOSNotifPrefs *prefs) {
  (void)prefs;
  return MuteBitfield_None;
}

extern ClickManager *app_state_get_click_manager(void);

ClickManager *modal_manager_get_click_manager(void) {
  return app_state_get_click_manager();
}

WindowStack *modal_manager_get_window_stack(ModalPriority priority) {
  (void)priority;
  return NULL;
}

WindowStack *window_manager_get_window_stack(ModalPriority priority) {
  (void)priority;
  return NULL;
}

void modal_window_push(Window *window, ModalPriority priority, bool animated) {
  (void)priority;
  app_window_stack_push(window, animated);
}

bool window_is_loaded(Window *window) {
  return window && window->is_loaded;
}

void window_set_overrides_back_button(Window *window, bool overrides_back_button) {
  if (window) {
    window->overrides_back_button = overrides_back_button;
  }
}

bool window_stack_remove(Window *window, bool animated) {
  (void)animated;
  return fw_window_stack_remove(window);
}

void vibes_cancel(void) {}
void light_system_color_release(void) {}
void peek_layer_destroy(PeekLayer *peek_layer) { (void)peek_layer; }
