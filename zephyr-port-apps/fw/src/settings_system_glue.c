/* SPDX-License-Identifier: Apache-2.0 */

// Service backends for the real settings/system.c on the qemu shell. Each
// reports the state the FreeRTOS reference reports under QEMU (dummy OTP
// serials, no stationary mode, default shell prefs) so the rendered screens
// are pixel-identical; destructive actions are no-ops.

#include <stdbool.h>
#include <zephyr/sys/printk.h>
#include <stdint.h>
#include <string.h>

#include "mfg/mfg_info.h"
#include "mfg/mfg_serials.h"
#include "shell/prefs.h"
#include "system/bootbits.h"
#include "system/version.h"

// --- mfg serials: QEMU has no OTP driver; the reference falls back to the
// dummy strings in mfg_serials.c.
void mfg_info_get_serialnumber(char *serial_number, size_t serial_number_size) {
  strncpy(serial_number, "XXXXXXXXXXXX", serial_number_size);
  if (serial_number_size > MFG_SERIAL_NUMBER_SIZE) {
    serial_number[MFG_SERIAL_NUMBER_SIZE] = '\0';
  }
}

void mfg_info_get_hw_version(char *hw_version, size_t hw_version_size) {
  strncpy(hw_version, "XXXXXXXX", hw_version_size);
  if (hw_version_size > MFG_HW_VERSION_SIZE) {
    hw_version[MFG_HW_VERSION_SIZE] = '\0';
  }
}

// --- version / boot (version.c provides the metadata + recovery readers)
uint32_t boot_version_read(void) { return 0; }

// --- i18n (English build, no language pack)
char *i18n_get_locale(void) { return "en_US"; }
uint16_t i18n_get_version(void) { return 0; }

// --- services with no QEMU behavior; reference reports these defaults
void light_allow(bool allowed) { (void)allowed; }
// Stand-By routes to the real (persisted) shell pref like the shipping
// stationary service; the motion inhibit logic itself is not needed for
// the qemu walks.
bool shell_prefs_get_stationary_enabled(void);
void shell_prefs_set_stationary_enabled(bool enabled);
bool stationary_get_enabled(void) { return shell_prefs_get_stationary_enabled(); }
void stationary_set_enabled(bool enabled) { shell_prefs_set_stationary_enabled(enabled); }
uint32_t ambient_light_level_to_lux(uint32_t light_level) { return light_level; }


// --- destructive / heavyweight actions: no-ops on the qemu shell
void core_dump_reset(bool force) { (void)force; }
void blob_db_compact_growable_dbs(void) {}
void battery_ui_handle_shut_down(void) {}
void settings_factory_reset_window_push(void) {}

// --- dialogs infrastructure: the port runs one shared window stack; modal
// dialogs land on it like app windows (reference renders them full-screen the
// same way under this walk).
struct WindowStack;
struct Window;
void fw_window_stack_push(struct Window *window);
void fw_window_stack_pop(void);
struct Window *fw_window_stack_top(void);

struct WindowStack *modal_manager_get_window_stack(int priority) {
  (void)priority;
  return (struct WindowStack *)1;  // opaque token; the port has one stack
}

// Modal dialogs land on the shared stack through the same load-running push
// as app windows (app_window_stack_push runs load/appear, wires the click
// config, then pushes) — pushing the raw stack skips the dialog's load
// handler and renders an empty window.
void app_window_stack_push(struct Window *window, bool animated);
void window_stack_push(struct WindowStack *stack, struct Window *window, bool animated) {
  (void)stack;
  app_window_stack_push(window, animated);
}

bool window_stack_remove(struct Window *window, bool animated) {
  (void)animated;
  if (fw_window_stack_top() == window) {
    fw_window_stack_pop();
    return true;
  }
  return false;
}

void vibes_short_pulse(void) {}
void vibes_cancel(void) {}
void vibes_double_pulse(void) {}

// Speaker service (emery has a speaker; QEMU audio unused for pixel walks).
#include "kernel/pebble_tasks.h"
void speaker_service_handle_audio_prefs_changed(void) {}
void speaker_service_play_volume_preview(void) {}
void speaker_service_set_owner_task(PebbleTask task) { (void)task; }
void speaker_service_stop_for_task(PebbleTask task) { (void)task; }
int32_t vibe_get_braking_strength(void) { return 0; }

struct WindowStack *window_manager_get_window_stack(int priority) {
  (void)priority;
  return (struct WindowStack *)1;
}

#include "applib/app_exit_reason.h"
void app_exit_reason_set(AppExitReason reason) { (void)reason; }
void vibes_set_default_vibe_strength(int32_t strength) { (void)strength; }

// Shell prefs the reference defaults to under QEMU.
#include "shell/system_theme.h"
static PreferredContentSize s_content_size = PreferredContentSizeDefault;

// Alert masks route through the real (PFS-persisted) alerts preferences.
#include "pbl/services/notifications/alerts.h"
#include "pbl/services/notifications/alerts_preferences_private.h"
#include "pbl/services/notifications/alerts_private.h"
AlertMask alerts_get_mask(void) { return alerts_preferences_get_alert_mask(); }
void alerts_set_mask(AlertMask mask) { alerts_preferences_set_alert_mask(mask); }
AlertMask alerts_get_dnd_mask(void) { return alerts_preferences_dnd_get_mask(); }
void alerts_set_dnd_mask(AlertMask mask) { alerts_preferences_dnd_set_mask(mask); }
void sys_vibe_pattern_enqueue_step_raw(uint32_t step_duration_ms, int32_t strength) {
  (void)step_duration_ms;
  (void)strength;
}
void sys_vibe_pattern_trigger_start(void) {}

#include "applib/ui/window.h"
bool window_is_loaded(Window *window) {
  return window && window->is_loaded;
}

#include "applib/ui/number_window.h"
void *applib_malloc(size_t size);
void *_applib_type_malloc_NumberWindow(void) {
  return applib_malloc(sizeof(NumberWindow));
}

#include "applib/ui/dialogs/expandable_dialog.h"
void *_applib_type_malloc_ExpandableDialog(void) {
  return applib_malloc(sizeof(ExpandableDialog));
}

// Quiet Time calendar coupling: no pins/events in the QEMU image.
bool calendar_event_is_ongoing(void) { return false; }

// expandable_dialog scroll hinting; the port's content-indicator shim tracks
// availability only.
#include "applib/ui/content_indicator.h"
bool content_indicator_configure_direction(ContentIndicator *content_indicator,
                                           ContentIndicatorDirection direction,
                                           const ContentIndicatorConfig *config) {
  (void)content_indicator;
  (void)direction;
  (void)config;
  return true;
}

// --- Timeline (Quick View) + Quick Launch prefs: reference defaults
// (shell/normal/prefs.c) in RAM.
#include "applib/ui/click.h"
static bool s_peek_enabled = false;  // observed reference default on emery QEMU
static uint16_t s_peek_before_time_m = 10;
// Reference state: Timeline settings already opened (no first-use dialog).
// NOTE: the first-use expandable dialog also crashes in the port — its
// ACTION_BAR_ICON_UP/DOWN gbitmap loads return NULL (resource path gap).
static uint8_t s_timeline_settings_opened;  // InitialVersion: first-use dialog, like the reference

// Reference defaults (shell/normal/prefs.c): taps = Health / Timeline Future,
// holds disabled (back's Quiet Time toggle is not shown by this screen).
ButtonId app_launch_button(void) { return BUTTON_ID_SELECT; }
#include "process_management/app_install_manager.h"
bool app_install_entry_is_quick_launch_visible_only(const AppInstallEntry *entry) {
  (void)entry;
  return false;
}

// shell_prefs_init reads board backlight/accel defaults; mirror the
// reference qemu board tables (src/fw/board/boards/board_qemu_emery.c).
#include "board/board.h"
const BoardConfig BOARD_CONFIG = {
  .backlight_on_percent = 100,
  .ambient_light_dark_threshold = 150,
  .backlight_default_color = 0x00ffffff,
};
const BoardConfigAccel BOARD_CONFIG_ACCEL = {
  .default_motion_sensitivity = 0,
};

// --- Background App: no worker task on the qemu shell (reference shows
// "No background apps").
#include "process_management/worker_manager.h"
static ProcessContext s_worker_context;  // install_id 0 = INSTALL_ID_INVALID
AppInstallId worker_manager_get_current_worker_id(void) { return 0; }
ProcessContext *worker_manager_get_task_context(void) { return &s_worker_context; }
void worker_manager_put_launch_worker_event(AppInstallId id) { (void)id; }
void worker_manager_set_default_install_id(AppInstallId id) { (void)id; }
void process_manager_put_kill_process_event(PebbleTask task, bool gracefully) {
  (void)task;
  (void)gracefully;
}

bool app_install_entry_has_worker(const AppInstallEntry *entry) {
  (void)entry;
  return false;  // no PBW workers in the qemu registry
}

struct WindowStack;
void switch_worker_confirm(AppInstallId new_worker_id, bool set_as_default,
                           struct WindowStack *window_stack) {
  (void)new_worker_id;
  (void)set_as_default;
  (void)window_stack;
}

// prefs.c hooks into services the qemu shell does not run; keep its state
// mirror + persistence, no-op the actuation like the QEMU reference where
// the underlying hardware path is absent.
void accel_manager_set_motion_backlight_enabled(bool enabled) { (void)enabled; }
// Phone-sync of prefs over blob_db; no phone connection on the qemu shell.
void prefs_sync_init(void) {}
// Touch service actuation (QEMU touch device is inert for these walks;
// prefs persist through the real shell prefs).
void touch_nav_master_changed(void) {}
void touch_nav_set_enabled(bool enabled) { (void)enabled; }
void touch_service_set_globally_enabled(bool enabled) { (void)enabled; }
void touch_set_backlight_enabled(bool enabled) { (void)enabled; }
bool activity_is_initialized(void) { return false; }
void activity_set_enabled(bool enabled) { (void)enabled; }
void ambient_light_set_dark_threshold(uint32_t threshold) { (void)threshold; }
void i18n_enable(bool enable) { (void)enable; }
void i18n_set_resource(uint32_t resource_id) { (void)resource_id; }
void timeline_peek_set_enabled(bool enabled) { (void)enabled; }
void timeline_peek_set_show_before_time(uint16_t before_time_m) { (void)before_time_m; }

// App-install uuid mapping over the port registry (prefs.c persists
// quick-launch/watchface picks as uuids).
#include "app_registry.h"
size_t fw_app_registry_count(void);
const FwAppRegistryEntry *fw_app_registry_get(size_t index);
bool app_install_get_uuid_for_install_id(AppInstallId install_id, Uuid *uuid_out) {
  const size_t count = fw_app_registry_count();
  for (size_t i = 0; i < count; ++i) {
    const FwAppRegistryEntry *reg = fw_app_registry_get(i);
    if (reg && reg->install_id == install_id && reg->md) {
      *uuid_out = reg->md->uuid;
      return true;
    }
  }
  return false;
}

AppInstallId app_install_get_id_for_uuid(const Uuid *uuid) {
  const size_t count = fw_app_registry_count();
  for (size_t i = 0; i < count; ++i) {
    const FwAppRegistryEntry *reg = fw_app_registry_get(i);
    if (reg && reg->md && uuid_equal(&reg->md->uuid, uuid)) {
      return reg->install_id;
    }
  }
  return 0;
}

typedef bool (*AppInstallEnumerateCb)(AppInstallEntry *entry, void *data);
void app_install_enumerate_entries(AppInstallEnumerateCb cb, void *data) {
  const size_t count = fw_app_registry_count();
  for (size_t i = 0; i < count; ++i) {
    const FwAppRegistryEntry *reg = fw_app_registry_get(i);
    if (!reg) {
      continue;
    }
    AppInstallEntry entry;
    if (app_install_get_entry_for_install_id(reg->install_id, &entry) &&
        !cb(&entry, data)) {
      return;
    }
  }
}

// --- bt mac string (qemu BT driver fixed AA identity address)
void bt_local_id_copy_address_mac_string(char *dest, size_t dest_size) {
  strncpy(dest, "AA:AA:AA:AA:AA:AA", dest_size);
  if (dest_size) {
    dest[dest_size - 1] = '\0';
  }
}
