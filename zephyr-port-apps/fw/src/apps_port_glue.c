/* SPDX-License-Identifier: Apache-2.0 */

// Port-side backends for the real PebbleOS system apps (watchfaces picker +
// settings shell). Everything here is glue the fw scaffold lacks: the real app
// UI (watchfaces.c, settings.c, menu.c, window.c, health.c) is compiled 1:1 and
// links against these. See zephyr-port-notes/SYSTEM-APPS-BUILDOUT.md.
//
// ponytail: these are the minimum shims to render the REAL app menus. Where a
// shipping subsystem (shell/prefs, i18n catalog, app_install_manager, activity
// service) is disproportionate to port, it is replaced by a RAM-backed store or
// identity function, marked below with its ceiling. The UI is real; the state
// behind it is intentionally shallow.

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/sys/printk.h>

#include "applib/graphics/gtypes.h"
#include "applib/graphics/graphics.h"
#include "applib/graphics/text.h"
#include "applib/fonts/fonts.h"
#include "applib/ui/layer.h"
#include "applib/ui/menu_cell_layer.h"
#include "applib/ui/status_bar_layer.h"
#include "applib/ui/window.h"

#include "process_management/app_install_manager.h"
#include "process_management/app_manager.h"
#include "process_management/app_menu_data_source.h"
#include "process_management/pebble_process_md.h"

#include "pbl/services/i18n/i18n.h"
#include "pbl/util/list.h"

#include "kernel/pbl_malloc.h"

#include "shell/prefs.h"
#include "shell/system_theme.h"

#include "apps/system/settings/menu.h"
#include "apps/system/settings/window.h"

#include "app_registry.h"

// ---------------------------------------------------------------------------
// App heap helpers the real apps expect (system_app.c already provides
// app_zalloc_check / app_free; the real apps also call the non-zeroing / -check
// variants). Backed by the same kernel heap.
// ---------------------------------------------------------------------------
void *app_malloc(size_t bytes) { return app_zalloc_check(bytes); }

// ---------------------------------------------------------------------------
// i18n: identity. The port ships one (English) locale, so a lookup returns the
// key string unchanged and there is nothing to free.
// ponytail: no translation catalog. Add i18n.c + a language resource for L10n.
// ---------------------------------------------------------------------------
const char *i18n_get(const char *string, const void *owner) {
  (void)owner;
  return string;
}

void i18n_get_with_buffer(const char *string, char *buffer, size_t length) {
  strncpy(buffer, string ? string : "", length ? length - 1 : 0);
  if (length) {
    buffer[length - 1] = '\0';
  }
}

size_t i18n_get_length(const char *string) { return string ? strlen(string) : 0; }

void i18n_free(const char *string, const void *owner) {
  (void)string;
  (void)owner;
}

void i18n_free_all(const void *owner) { (void)owner; }

// ---------------------------------------------------------------------------
// shell/prefs: a flat RAM store for the handful of prefs the ported menus read.
// ponytail: not persisted (shipping keeps these in the settings_file/blob_db).
// Wire shell/prefs.c + a settings file to survive a reboot.
// ---------------------------------------------------------------------------
static UnitsDistance s_units_distance = UnitsDistance_KM;

UnitsDistance shell_prefs_get_units_distance(void) { return s_units_distance; }
void shell_prefs_set_units_distance(UnitsDistance unit) { s_units_distance = unit; }

GColor shell_prefs_get_theme_highlight_color(void) {
  return PBL_IF_COLOR_ELSE(GColorJazzberryJam, GColorBlack);
}

bool shell_prefs_get_menu_scroll_wrap_around_enable(void) { return false; }

static MenuScrollVibeBehavior s_scroll_vibe = MenuScrollNoVibe;
MenuScrollVibeBehavior shell_prefs_get_menu_scroll_vibe_behavior(void) { return s_scroll_vibe; }
void shell_prefs_set_menu_scroll_vibe_behavior(MenuScrollVibeBehavior behavior) {
  s_scroll_vibe = behavior;
}

// ---------------------------------------------------------------------------
// Default watchface: RAM. The watchfaces picker reads this to mark the "Active"
// row and writes it on SELECT (via app_manager_put_launch_app_event below).
// ---------------------------------------------------------------------------
static AppInstallId s_default_watchface_id = INSTALL_ID_INVALID;

AppInstallId watchface_get_default_install_id(void) { return s_default_watchface_id; }
void watchface_set_default_install_id(AppInstallId id) { s_default_watchface_id = id; }

// Selecting a watchface row: record it as the active face. Shipping relaunches
// the shell with the chosen face; the port just persists the choice + logs it so
// the picker's "Active" subtitle tracks the selection.
// ponytail: does not switch the running watchface. Route through the shell's
// launch path once the shell/watchface service is ported.
void app_manager_put_launch_app_event(const AppLaunchEventConfig *config) {
  if (!config) {
    return;
  }
  s_default_watchface_id = config->id;
  printk("WATCHFACE_SET %" PRId32 "\n", config->id);
}

// ---------------------------------------------------------------------------
// app_install_manager entry predicates the watchfaces filter uses. Ported 1:1
// from app_install_manager.c (the port synthesizes AppInstallEntry values from
// the fw registry in the data source below).
// ---------------------------------------------------------------------------
bool app_install_entry_is_watchface(const AppInstallEntry *entry) {
  return entry->process_type == ProcessTypeWatchface;
}

bool app_install_entry_is_hidden(const AppInstallEntry *entry) {
  return entry->visibility != ProcessVisibilityShown;
}

// ---------------------------------------------------------------------------
// system_theme font lookup used by menu_layer_system_cells.c. The port has one
// system font (GOTHIC_14, served by watchface_sandboxed/port.c); return it for
// every text style.
// ponytail: single font. Load the real per-style fonts for correct sizing.
// ---------------------------------------------------------------------------
GFont system_theme_get_font_for_default_size(TextStyleFont font) {
  (void)font;
  return fonts_get_system_font("RESOURCE_ID_GOTHIC_14");
}

// Icons are cosmetic in the port; graphics_draw_bitmap_in_rect is a no-op
// already provided by ui_shims.c (the data source hands out a 1x1 blank so the
// real draw path's gbitmap_get_format() is safe).

// ---------------------------------------------------------------------------
// activity service prefs (health submodule). RAM-backed toggles; start/stop are
// no-ops (no activity tracking service in the port).
// ponytail: no real tracking. Port services/activity for live metrics.
// ---------------------------------------------------------------------------
static bool s_activity_tracking_enabled = true;

bool activity_prefs_tracking_is_enabled(void) { return s_activity_tracking_enabled; }
void activity_prefs_tracking_set_enabled(bool enable) { s_activity_tracking_enabled = enable; }
bool activity_start_tracking(bool test_mode) {
  (void)test_mode;
  return true;
}
bool activity_stop_tracking(void) { return true; }

// ---------------------------------------------------------------------------
// AppMenuDataSource: a port implementation of the public interface backed by the
// fw app registry's watchface entries, instead of the live app_install_manager.
// This keeps watchfaces.c (the real picker UI + filter) byte-for-byte real.
// ponytail: static list (no install/remove events, no per-app icons or ordering).
// ---------------------------------------------------------------------------
void app_menu_data_source_init(AppMenuDataSource *source,
                               const AppMenuDataSourceCallbacks *callbacks,
                               void *callback_context) {
  memset(source, 0, sizeof(*source));
  source->callback_context = callback_context;
  if (callbacks) {
    source->callbacks = *callbacks;
  }
}

static void prv_load_if_needed(AppMenuDataSource *source) {
  if (source->is_list_loaded) {
    return;
  }
  source->is_list_loaded = true;

  const size_t count = fw_app_registry_count();
  for (size_t i = 0; i < count; ++i) {
    const FwAppRegistryEntry *reg = fw_app_registry_get(i);
    if (!reg || !reg->md) {
      continue;
    }
    const PebbleProcessMdSystem *md = (const PebbleProcessMdSystem *)reg->md;

    AppInstallEntry entry = {
      .install_id = reg->install_id,
      .visibility = reg->md->visibility,
      .process_type = reg->md->process_type,
      .uuid = reg->md->uuid,
    };
    strncpy(entry.name, md->name ? md->name : "", sizeof(entry.name) - 1);

    if (source->callbacks.filter && !source->callbacks.filter(source, &entry)) {
      continue;
    }

    AppMenuNode *node = app_malloc_check(sizeof(*node));
    memset(node, 0, sizeof(*node));
    node->install_id = reg->install_id;
    node->uuid = entry.uuid;
    node->visibility = entry.visibility;
    node->icon = source->default_icon;

    const char *name = md->name ? md->name : "";
    const size_t len = strlen(name) + 1;
    node->name = app_malloc_check(len);
    memcpy(node->name, name, len);

    source->list = (AppMenuNode *)list_append(&source->list->node, &node->node);
  }
}

void app_menu_data_source_deinit(AppMenuDataSource *source) {
  AppMenuNode *node = source->list;
  while (node) {
    AppMenuNode *next = (AppMenuNode *)list_get_next((ListNode *)node);
    app_free(node->name);
    app_free(node);
    node = next;
  }
  source->list = NULL;
  if (source->default_icon) {
    gbitmap_destroy(source->default_icon);
    source->default_icon = NULL;
  }
  source->is_list_loaded = false;
}

void app_menu_data_source_enable_icons(AppMenuDataSource *source, uint32_t fallback_icon_id) {
  (void)fallback_icon_id;
  source->show_icons = true;
  // A valid 1x1 bitmap so the real draw path's gbitmap_get_format() is safe; the
  // draw itself is a no-op (see graphics_draw_bitmap_in_rect above).
  source->default_icon = gbitmap_create_blank(GSize(1, 1), GBitmapFormat1Bit);
}

static uint16_t prv_transform_index(AppMenuDataSource *source, uint16_t index) {
  if (source->callbacks.transform_index) {
    return source->callbacks.transform_index(source, index, source->callback_context);
  }
  return index;
}

AppMenuNode *app_menu_data_source_get_node_at_index(AppMenuDataSource *source, uint16_t row_index) {
  prv_load_if_needed(source);
  return (AppMenuNode *)list_get_at((ListNode *)source->list, prv_transform_index(source, row_index));
}

uint16_t app_menu_data_source_get_count(AppMenuDataSource *source) {
  prv_load_if_needed(source);
  return (uint16_t)list_count((ListNode *)source->list);
}

uint16_t app_menu_data_source_get_index_of_app_with_install_id(AppMenuDataSource *source,
                                                               AppInstallId install_id) {
  prv_load_if_needed(source);
  AppMenuNode *node = source->list;
  uint16_t index = 0;
  while (node) {
    if (node->install_id == install_id) {
      return prv_transform_index(source, index);
    }
    node = (AppMenuNode *)list_get_next((ListNode *)node);
    ++index;
  }
  return MENU_INDEX_NOT_FOUND;
}

GBitmap *app_menu_data_source_get_node_icon(AppMenuDataSource *source, AppMenuNode *node) {
  if (!node->icon && source->show_icons) {
    node->icon = source->default_icon;
  }
  return node->icon;
}

// ---------------------------------------------------------------------------
// Settings submodules not yet ported. menu.c's registry references every
// settings_*_get_info; the ones below open a real settings_window (shell UI)
// with a single "Not ported" row so the shell menu is fully navigable and each
// sub-screen renders. The real submodules (their service closures are large) are
// the follow-on port; health is done for real (health.c compiled).
// ponytail: replace each STUB_SUBMODULE with the real submodule .c as its
// service dependencies land.
// ---------------------------------------------------------------------------
#define STUB_SUBMODULE(fn, disp_name, category)                                            \
  static uint16_t fn##_num_rows(SettingsCallbacks *c) {                                     \
    (void)c;                                                                                \
    return 1;                                                                               \
  }                                                                                         \
  static void fn##_draw_row(SettingsCallbacks *c, GContext *ctx, const Layer *cell_layer,   \
                            uint16_t row, bool selected) {                                  \
    (void)c;                                                                                \
    (void)row;                                                                              \
    (void)selected;                                                                         \
    menu_cell_basic_draw(ctx, cell_layer, disp_name, "Not ported", NULL);                   \
  }                                                                                         \
  static void fn##_deinit(SettingsCallbacks *c) { app_free(c); }                            \
  static Window *fn##_init(void) {                                                          \
    SettingsCallbacks *cb = app_zalloc_check(sizeof(*cb));                                  \
    cb->num_rows = fn##_num_rows;                                                           \
    cb->draw_row = fn##_draw_row;                                                           \
    cb->deinit = fn##_deinit;                                                               \
    return settings_window_create(category, cb);                                            \
  }                                                                                         \
  const SettingsModuleMetadata *fn(void) {                                                  \
    static const SettingsModuleMetadata md = {.name = disp_name, .init = fn##_init};        \
    return &md;                                                                             \
  }

STUB_SUBMODULE(settings_bluetooth_get_info, "Bluetooth", SettingsMenuItemBluetooth)
STUB_SUBMODULE(settings_notifications_get_info, "Notifications", SettingsMenuItemNotifications)
STUB_SUBMODULE(settings_vibe_patterns_get_info, "Vibrations", SettingsMenuItemVibrations)
STUB_SUBMODULE(settings_quiet_time_get_info, "Quiet Time", SettingsMenuItemQuietTime)
STUB_SUBMODULE(settings_timeline_get_info, "Timeline", SettingsMenuItemTimeline)
STUB_SUBMODULE(settings_activity_tracker_get_info, "Background App", SettingsMenuItemActivity)
STUB_SUBMODULE(settings_quick_launch_get_info, "Quick Launch", SettingsMenuItemQuickLaunch)
STUB_SUBMODULE(settings_time_get_info, "Date & Time", SettingsMenuItemDateTime)
STUB_SUBMODULE(settings_display_get_info, "Display", SettingsMenuItemDisplay)
STUB_SUBMODULE(settings_system_get_info, "System", SettingsMenuItemSystem)
