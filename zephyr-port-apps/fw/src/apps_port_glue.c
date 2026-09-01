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
#include "launcher_ui.h"
#include "system_app.h"

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
// i18n_ctx_noop() keys carry a "context\4" prefix; the untranslated fallback is
// the text after the separator (shipping strips it the same way).
static const char *prv_i18n_strip_ctx(const char *string) {
  const char *sep = string ? strchr(string, '\4') : NULL;
  return sep ? sep + 1 : string;
}

const char *i18n_get(const char *string, const void *owner) {
  (void)owner;
  return prv_i18n_strip_ctx(string);
}

void i18n_get_with_buffer(const char *string, char *buffer, size_t length) {
  string = prv_i18n_strip_ctx(string);
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
  // Match shipping PebbleOS: the default menu selection accent is Vivid Cerulean
  // (teal), not Jazzberry Jam (magenta). See shell/normal/prefs.c.
  return PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorBlack);
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
// TicToc (registry id -1), the shipping default face; the launcher's
// Watchfaces glance shows its name as the subtitle.
static AppInstallId s_default_watchface_id = -1;

AppInstallId watchface_get_default_install_id(void) { return s_default_watchface_id; }
void watchface_set_default_install_id(AppInstallId id) { s_default_watchface_id = id; }

// Selecting a watchface row: record it as the active face. Shipping relaunches
// the shell with the chosen face; the port just persists the choice + logs it so
// the picker's "Active" subtitle tracks the selection.
// ponytail: does not switch the running watchface. Route through the shell's
// launch path once the shell/watchface service is ported.
struct CompositorTransition;
void fw_compositor_request_transition(const struct CompositorTransition *impl,
                                      uint16_t first_sample_ms);
const struct CompositorTransition *compositor_launcher_app_transition_get(
    bool app_is_destination);
void fw_compositor_skip_focus_dup(void);

void app_manager_put_launch_app_event(const AppLaunchEventConfig *config) {
  if (!config) {
    return;
  }

  // A non-watchface md-backed entry (real launcher SELECT): leave the current
  // app (the launcher) and launch the target through the shared pump.
  const FwAppRegistryEntry *entry = fw_app_registry_find_by_id(config->id);
  if (entry && entry->md && entry->md->process_type != ProcessTypeWatchface) {
    printk("SHELL_LAUNCH %" PRId32 " %s\n", config->id, entry->name);
    // Shipping (shell.c): launcher -> app uses the launcher-app moook slide;
    // the destination's focus render is the stream tail (no extra dup).
    fw_compositor_skip_focus_dup();
    fw_compositor_request_transition(
        compositor_launcher_app_transition_get(true /* app_is_destination */), 0);
    fw_shell_request_launch(entry->md);
    fw_system_app_request_exit();
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
// system_theme font lookup used by menu_layer_system_cells.c. Mirror the real
// s_text_styles[PreferredContentSizeLarge] table (system_theme.c) — the port's
// default content size is Large — so each style resolves to the shipping GOTHIC
// face. fonts_get_system_font() (watchface_sandboxed/port.c) turns the key into
// the matching loaded FontInfo.
// ponytail: LECO time-header numbers aren't embedded; they fall back to GOTHIC_14.
// ---------------------------------------------------------------------------

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
    node->app_num = SYSTEM_APP;
    node->icon_resource_id = ((const PebbleProcessMdSystem *)reg->md)->icon_resource_id;

    const char *name = md->name ? md->name : "";
    const size_t len = strlen(name) + 1;
    node->name = app_malloc_check(len);
    memcpy(node->name, name, len);

    // list_append returns the appended (tail) node; keep the HEAD in
    // source->list or every node before the last one becomes unreachable.
    if (source->list) {
      (void)list_append(&source->list->node, &node->node);
    } else {
      source->list = node;
    }
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

#if !defined(CONFIG_BOARD_QEMU_EMERY)
STUB_SUBMODULE(settings_bluetooth_get_info, "Bluetooth", SettingsMenuItemBluetooth)
#endif
#if !defined(CONFIG_BOARD_QEMU_EMERY)
STUB_SUBMODULE(settings_notifications_get_info, "Notifications", SettingsMenuItemNotifications)
#endif
#if !defined(CONFIG_BOARD_QEMU_EMERY)
STUB_SUBMODULE(settings_vibe_patterns_get_info, "Sounds & Haptics", SettingsMenuItemVibrations)
#endif
#if !defined(CONFIG_BOARD_QEMU_EMERY)
STUB_SUBMODULE(settings_quiet_time_get_info, "Quiet Time", SettingsMenuItemQuietTime)
#endif
STUB_SUBMODULE(settings_timeline_get_info, "Timeline", SettingsMenuItemTimeline)
STUB_SUBMODULE(settings_activity_tracker_get_info, "Background App", SettingsMenuItemActivity)
STUB_SUBMODULE(settings_quick_launch_get_info, "Quick Launch", SettingsMenuItemQuickLaunch)
#if !defined(CONFIG_BOARD_QEMU_EMERY)
STUB_SUBMODULE(settings_time_get_info, "Date & Time", SettingsMenuItemDateTime)
#endif
#if !defined(CONFIG_BOARD_QEMU_EMERY)
STUB_SUBMODULE(settings_display_get_info, "Display", SettingsMenuItemDisplay)
#endif
#if !defined(CONFIG_BOARD_QEMU_EMERY)
STUB_SUBMODULE(settings_system_get_info, "System", SettingsMenuItemSystem)
#endif

// ---------------------------------------------------------------------------
// Bluetooth submodule backends (qemu shell): the real settings/bluetooth.c is
// compiled 1:1 and links against these. They report the same state the
// FreeRTOS reference reports under QEMU: airplane mode off (shell_glue.c), no
// paired remotes, and the "Pebble AAAA" name derived from the qemu BT driver's
// fixed AA:AA:AA:AA:AA:AA identity address (see src/bluetooth-fw/qemu/id.c +
// services/bluetooth/local_id.c).
// ---------------------------------------------------------------------------
#if defined(CONFIG_BOARD_QEMU_EMERY)
#include <stdio.h>

#include <bluetooth/bluetooth_types.h>
#include <bluetooth/sm_types.h>

#include "apps/system/settings/bluetooth.h"
#include "apps/system/settings/remote.h"

void bt_local_id_copy_device_name(char name_out[BT_DEVICE_NAME_BUFFER_SIZE], bool is_le) {
  (void)is_le;
  snprintf(name_out, BT_DEVICE_NAME_BUFFER_SIZE, "Pebble %02X%02X", 0xAA, 0xAA);
}

void bt_persistent_storage_for_each_ble_pairing(
    void (*cb)(BTDeviceInternal *device, SMIdentityResolvingKey *irk, const char *name,
               BTBondingID *id, void *context),
    void *context) {
  (void)cb;
  (void)context;
}

bool bt_persistent_storage_get_ble_pairing_by_id(BTBondingID bonding,
                                                 SMIdentityResolvingKey *irk_out,
                                                 BTDeviceInternal *device_out, char *name_out) {
  (void)bonding;
  (void)irk_out;
  (void)device_out;
  (void)name_out;
  return false;
}

// ponytail: airplane-mode toggle is inert (no BT stack state machine in the
// port); flipping it for real needs shell_glue's flag + a PEBBLE_BT_STATE_EVENT.
void bt_ctl_set_airplane_mode_async(bool enabled) { (void)enabled; }

void bt_lock(void) {}
void bt_unlock(void) {}

GAPLEConnection *gap_le_connection_find_by_irk(const SMIdentityResolvingKey *irk) {
  (void)irk;
  return NULL;
}

GAPLEConnection *gap_le_connection_by_device(const BTDeviceInternal *device) {
  (void)device;
  return NULL;
}

void gap_le_device_name_request(const BTDeviceInternal *address) { (void)address; }
void gap_le_device_name_request_all(void) {}

void bt_pairability_use(void) {}
void bt_pairability_release(void) {}

// No remotes ever exist under QEMU, so the per-remote action menu is
// unreachable; satisfy the link.
void settings_remote_menu_push(struct SettingsBluetoothData *bt_data, StoredRemote *stored_remote) {
  (void)bt_data;
  (void)stored_remote;
}
#endif  // CONFIG_BOARD_QEMU_EMERY

// ---------------------------------------------------------------------------
// Display submodule backends (qemu shell): RAM-backed backlight/touch/language
// state seeded to what the FreeRTOS reference reports under QEMU with the
// shared qemu_spi_flash.bin prefs (preset Advanced, backlight + touch on,
// English). ponytail: values are seeded, not read from the prefs DB; wire
// shell/normal/prefs.c + settings_file to track the flash image instead.
// ---------------------------------------------------------------------------
#if defined(CONFIG_BOARD_QEMU_EMERY)
static bool s_backlight_enabled = true;
static uint8_t s_backlight_preset = BacklightPreset_Advanced;
static bool s_ambient_sensor_enabled = true;
static uint8_t s_backlight_intensity = 50;  // BACKLIGHT_INTENSITY_HIGH (Standard preset)
static uint32_t s_backlight_timeout_ms = DEFAULT_BACKLIGHT_TIMEOUT_MS;
static bool s_backlight_motion = true;
static BacklightTouchWake s_backlight_touch_wake = BacklightTouchWake_DoubleTap;
static bool s_touch_enabled = true;
static bool s_touch_nav_menu = true;
static ShellLanguage s_language = ShellLanguageEnglish;
static uint8_t s_legacy_app_render_mode = 1;  // shipping default: Scaled (Nearest)

bool backlight_is_enabled(void) { return s_backlight_enabled; }
void light_toggle_enabled(void) { s_backlight_enabled = !s_backlight_enabled; }
BacklightPreset backlight_get_preset(void) { return s_backlight_preset; }
void backlight_set_preset(BacklightPreset preset) { s_backlight_preset = preset; }
uint8_t backlight_get_intensity(void) { return s_backlight_intensity; }
void backlight_set_intensity(uint8_t intensity) { s_backlight_intensity = intensity; }
uint32_t backlight_get_timeout_ms(void) { return s_backlight_timeout_ms; }
void backlight_set_timeout_ms(uint32_t timeout_ms) { s_backlight_timeout_ms = timeout_ms; }
bool backlight_is_motion_enabled(void) { return s_backlight_motion; }
void backlight_set_motion_enabled(bool enable) { s_backlight_motion = enable; }
bool backlight_is_ambient_sensor_enabled(void) { return s_ambient_sensor_enabled; }
void light_toggle_ambient_sensor_enabled(void) {
  s_ambient_sensor_enabled = !s_ambient_sensor_enabled;
}
BacklightTouchWake backlight_get_touch_wake(void) { return s_backlight_touch_wake; }
void backlight_set_touch_wake(BacklightTouchWake wake) { s_backlight_touch_wake = wake; }
void light_enable_interaction(void) {}
uint32_t light_get_ambient_lux(void) { return 0; }
void ambient_light_prime(void) {}
void ambient_light_release(void) {}

bool touch_is_globally_enabled(void) { return s_touch_enabled; }
void touch_set_globally_enabled(bool enabled) { s_touch_enabled = enabled; }
bool touch_navigation_menu_is_enabled(void) { return s_touch_nav_menu; }
void touch_set_navigation_menu_enabled(bool enabled) { s_touch_nav_menu = enabled; }

ShellLanguage shell_prefs_get_language(void) { return s_language; }
void shell_prefs_set_language(ShellLanguage language) { s_language = language; }
char *i18n_get_lang_name(void) { return "English"; }

uint8_t shell_prefs_get_legacy_app_render_mode(void) { return s_legacy_app_render_mode; }
void shell_prefs_set_legacy_app_render_mode(uint8_t mode) { s_legacy_app_render_mode = mode; }
#endif  // CONFIG_BOARD_QEMU_EMERY

// option_menu_window.c is compiled against the generated applib_malloc header
// on qemu (its TU resolves build/src/fw first for the real resource IDs), so
// its typed allocator resolves here, onto the same applib heap.
#if defined(CONFIG_BOARD_QEMU_EMERY)
#include "applib/ui/option_menu_window.h"
void *applib_malloc(size_t size);
void *_applib_type_malloc_OptionMenu(void) { return applib_malloc(sizeof(OptionMenu)); }
#endif

// ---------------------------------------------------------------------------
// Date & Time submodule backends (qemu shell): the reference under QEMU runs
// with automatic time + automatic timezone and no timezone set, so the menu
// shows Time Source/Automatic, Time Format/12h, Timezone Source/Automatic.
// ponytail: toggles flip RAM state only; clock_set_time and the phone time
// request are inert (no clock service / phone in the port).
// ---------------------------------------------------------------------------
#if defined(CONFIG_BOARD_QEMU_EMERY)
#include <time.h>

#include <zephyr/sys/timeutil.h>

#include "pbl/services/clock.h"

time_t rtc_get_time(void);

static bool s_clock_manual_time;
static bool s_clock_manual_timezone;
static bool s_clock_timezone_set;
static int16_t s_clock_timezone_region = -1;

bool clock_time_source_is_manual(void) { return s_clock_manual_time; }
void clock_set_manual_time_source(bool manual) { s_clock_manual_time = manual; }
bool clock_timezone_source_is_manual(void) { return s_clock_manual_timezone; }
void clock_set_manual_timezone_source(bool manual) { s_clock_manual_timezone = manual; }
bool clock_is_timezone_set(void) { return s_clock_timezone_set; }

void clock_set_timezone_by_region_id(uint16_t region_id) {
  s_clock_timezone_region = (int16_t)region_id;
  s_clock_timezone_set = true;
}

void clock_get_timezone_region(char *region_name, const size_t buffer_size) {
  if (region_name && buffer_size) {
    region_name[0] = '\0';
  }
}

void clock_request_time_from_phone(void) {}

void clock_set_time(time_t new_time) { (void)new_time; }

void clock_get_time_tm(struct tm *time_tm) {
  const time_t now = rtc_get_time();
  gmtime_r(&now, time_tm);
}

void clock_set_24h_style(bool is_24h) { (void)is_24h; }

// Minimal-libc gap; the port's wall clock is UTC, so mktime == timegm.
time_t mktime(struct tm *tm_val) { return timeutil_timegm(tm_val); }

int16_t shell_prefs_get_automatic_timezone_id(void) { return -1; }
#endif  // CONFIG_BOARD_QEMU_EMERY
