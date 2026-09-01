/* SPDX-License-Identifier: Apache-2.0 */

// Inert stubs for the applib helper windows the Alarms app reaches only on user
// interaction (new-alarm editor time/day pickers, alarm-detail action menu and
// option menus, and the confirmation / first-run dialogs). The Alarms app
// launches straight into its real alarm-list menu (app_alarm_stubs.c serves one
// canned alarm), which renders with the real menu_layer + menu_cell_layer; these
// deeper windows are never entered during launch/first render.
//
// ponytail: navigating into the editor / detail / dialogs is a no-op here. These
// are full applib windows (time_selection_window.c, day_picker.c, the dialog
// family, action_menu_window.c, option_menu_window.c) that can be compiled in
// wholesale later; they were stubbed to keep the Alarms launch+render closure
// small, since none render at launch.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "applib/ui/action_menu_window.h"
#include "applib/ui/day_picker.h"
#include "applib/ui/dialogs/dialog.h"
#include "applib/ui/dialogs/expandable_dialog.h"
#include "applib/ui/dialogs/simple_dialog.h"
#include "applib/ui/date_selection_window.h"
#include "applib/ui/option_menu_window.h"
#include "applib/ui/time_selection_window.h"
#include "applib/ui/window.h"
#include "apps/system/settings/option_menu.h"

// --- new-alarm editor: time selection + day picker --------------------------
void time_selection_window_init(TimeSelectionWindowData *w, const TimeSelectionWindowConfig *c) {
  (void)w;
  (void)c;
}

void time_selection_window_configure(TimeSelectionWindowData *w,
                                     const TimeSelectionWindowConfig *c) {
  (void)w;
  (void)c;
}

void time_selection_window_set_to_current_time(TimeSelectionWindowData *w) { (void)w; }

void time_selection_window_deinit(TimeSelectionWindowData *w) { (void)w; }

void date_selection_window_init(DateSelectionWindowData *w, const char *label, GColor color,
                                DateSelectionCompleteCallback complete, void *context) {
  (void)w;
  (void)label;
  (void)color;
  (void)complete;
  (void)context;
}

void date_selection_window_set_to_current_date(DateSelectionWindowData *w) { (void)w; }

void date_selection_window_deinit(DateSelectionWindowData *w) { (void)w; }

void day_picker_push(DayPickerConfig config, DayPickerCallback callback, void *context) {
  (void)config;
  (void)callback;
  (void)context;
}

// --- alarm-detail: action menu + option menus -------------------------------
ActionMenu *app_action_menu_open(ActionMenuConfig *config) {
  (void)config;
  return NULL;
}

ActionMenuLevel *action_menu_get_root_level(ActionMenu *action_menu) {
  (void)action_menu;
  return NULL;
}

void action_menu_set_result_window(ActionMenu *action_menu, Window *result_window) {
  (void)action_menu;
  (void)result_window;
}

// On qemu the real option_menu_window.c + settings/option_menu.c are compiled
// (the Display submenu pushes real option menus); pt2 keeps the inert stubs.
#if !defined(CONFIG_BOARD_QEMU_EMERY)
void option_menu_set_highlight_colors(OptionMenu *option_menu, GColor background,
                                      GColor foreground) {
  (void)option_menu;
  (void)background;
  (void)foreground;
}

OptionMenu *settings_option_menu_create(const char *i18n_title_key,
                                        OptionMenuContentType content_type, int choice,
                                        const OptionMenuCallbacks *callbacks, uint16_t num_rows,
                                        bool icons_enabled, const char **rows, void *context) {
  (void)i18n_title_key;
  (void)content_type;
  (void)choice;
  (void)callbacks;
  (void)num_rows;
  (void)icons_enabled;
  (void)rows;
  (void)context;
  return NULL;
}

void *settings_option_menu_get_context(SettingsOptionMenuData *data) {
  (void)data;
  return NULL;
}
#endif  // !CONFIG_BOARD_QEMU_EMERY

// --- confirmation / first-run dialogs ---------------------------------------
SimpleDialog *simple_dialog_create(const char *dialog_name) {
  (void)dialog_name;
  return NULL;
}

Dialog *simple_dialog_get_dialog(SimpleDialog *simple_dialog) {
  (void)simple_dialog;
  return NULL;
}

void app_simple_dialog_push(SimpleDialog *simple_dialog) { (void)simple_dialog; }

ExpandableDialog *expandable_dialog_create_with_params(const char *dialog_name, ResourceId icon,
                                                       const char *text, GColor text_color,
                                                       GColor background_color,
                                                       DialogCallbacks *callbacks,
                                                       ResourceId select_icon,
                                                       ClickHandler select_click_handler) {
  (void)dialog_name;
  (void)icon;
  (void)text;
  (void)text_color;
  (void)background_color;
  (void)callbacks;
  (void)select_icon;
  (void)select_click_handler;
  return NULL;
}

void expandable_dialog_set_header(ExpandableDialog *expandable_dialog, const char *header) {
  (void)expandable_dialog;
  (void)header;
}

void expandable_dialog_set_header_font(ExpandableDialog *expandable_dialog, GFont header_font) {
  (void)expandable_dialog;
  (void)header_font;
}

void expandable_dialog_set_action_bar_background_color(ExpandableDialog *expandable_dialog,
                                                       GColor color) {
  (void)expandable_dialog;
  (void)color;
}

void expandable_dialog_pop(ExpandableDialog *expandable_dialog) { (void)expandable_dialog; }
