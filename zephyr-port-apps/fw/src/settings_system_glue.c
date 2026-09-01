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

bool window_stack_push(struct WindowStack *stack, struct Window *window, bool animated) {
  (void)stack;
  (void)animated;
  fw_window_stack_push(window);
  return true;
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

#include "applib/ui/window.h"
bool window_is_loaded(Window *window) {
  return window && window->is_loaded;
}

#include "applib/ui/number_window.h"
void *applib_malloc(size_t size);
void *_applib_type_malloc_NumberWindow(void) {
  return applib_malloc(sizeof(NumberWindow));
}

// --- bt mac string (qemu BT driver fixed AA identity address)
void bt_local_id_copy_address_mac_string(char *dest, size_t dest_size) {
  strncpy(dest, "AA:AA:AA:AA:AA:AA", dest_size);
  if (dest_size) {
    dest[dest_size - 1] = '\0';
  }
}
