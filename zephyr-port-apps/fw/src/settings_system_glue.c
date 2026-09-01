/* SPDX-License-Identifier: Apache-2.0 */

// Service backends for the real settings/system.c on the qemu shell. Each
// reports the state the FreeRTOS reference reports under QEMU (dummy OTP
// serials, no stationary mode, default shell prefs) so the rendered screens
// are pixel-identical; destructive actions are no-ops.

#include <stdbool.h>
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
bool stationary_get_enabled(void) { return true; }  // reference default: Stand-By Mode On
void stationary_set_enabled(bool enabled) { (void)enabled; }
uint32_t ambient_light_level_to_lux(uint32_t light_level) { return light_level; }
uint32_t backlight_get_ambient_threshold(void) { return 0; }
void backlight_set_ambient_threshold(uint32_t threshold) { (void)threshold; }

bool shell_prefs_get_vibe_log_info_enabled(void) { return false; }
void shell_prefs_set_vibe_log_info_enabled(bool enabled) { (void)enabled; }
bool shell_prefs_get_accel_shake_log_info_enabled(void) { return false; }
void shell_prefs_set_accel_shake_log_info_enabled(bool enabled) { (void)enabled; }
bool shell_prefs_can_coredump_on_request(void) { return false; }
void shell_prefs_set_coredump_on_request(bool enabled) { (void)enabled; }

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

void window_stack_push(struct WindowStack *stack, struct Window *window, bool animated) {
  (void)stack;
  (void)animated;
  fw_window_stack_push(window);
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
PreferredContentSize system_theme_get_content_size(void) { return s_content_size; }
void system_theme_set_content_size(PreferredContentSize size) { s_content_size = size; }

#include "pbl/services/notifications/alerts.h"
#include "pbl/services/notifications/alerts_private.h"
static AlertMask s_alert_mask = AlertMaskAllOn;
static AlertMask s_dnd_mask = AlertMaskAllOff;  // reference default: Quiet All Notifications
AlertMask alerts_get_mask(void) { return s_alert_mask; }
void alerts_set_mask(AlertMask mask) { s_alert_mask = mask; }
AlertMask alerts_get_dnd_mask(void) { return s_dnd_mask; }
void alerts_set_dnd_mask(AlertMask mask) { s_dnd_mask = mask; }
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
bool timeline_peek_prefs_get_enabled(void) { return s_peek_enabled; }
void timeline_peek_prefs_set_enabled(bool enabled) { s_peek_enabled = enabled; }
uint16_t timeline_peek_prefs_get_before_time(void) { return s_peek_before_time_m; }
void timeline_peek_prefs_set_before_time(uint16_t m) { s_peek_before_time_m = m; }
uint8_t timeline_prefs_get_settings_opened(void) { return s_timeline_settings_opened; }
void timeline_prefs_set_settings_opened(uint8_t version) { s_timeline_settings_opened = version; }

// Reference defaults (shell/normal/prefs.c): taps = Health / Timeline Future,
// holds disabled (back's Quiet Time toggle is not shown by this screen).
int32_t quick_launch_get_app(ButtonId button) {
  (void)button;
  return 0;  // INSTALL_ID_INVALID: hold slots disabled
}
int32_t quick_launch_single_click_get_app(ButtonId button) {
  if (button == BUTTON_ID_UP) {
    return -82;  // Health (registry id, matches FW_APP enumeration)
  }
  if (button == BUTTON_ID_DOWN) {
    return -10;  // Timeline Future
  }
  return 0;
}
ButtonId app_launch_button(void) { return BUTTON_ID_SELECT; }
void quick_launch_set_app(ButtonId button, int32_t id) { (void)button; (void)id; }
void quick_launch_set_enabled(ButtonId button, bool enabled) { (void)button; (void)enabled; }
void quick_launch_single_click_set_app(ButtonId button, int32_t id) { (void)button; (void)id; }
void quick_launch_single_click_set_enabled(ButtonId button, bool enabled) {
  (void)button;
  (void)enabled;
}
struct AppInstallEntry;
bool app_install_entry_is_quick_launch_visible_only(const struct AppInstallEntry *entry) {
  (void)entry;
  return false;
}

// --- bt mac string (qemu BT driver fixed AA identity address)
void bt_local_id_copy_address_mac_string(char *dest, size_t dest_size) {
  strncpy(dest, "AA:AA:AA:AA:AA:AA", dest_size);
  if (dest_size) {
    dest[dest_size - 1] = '\0';
  }
}
