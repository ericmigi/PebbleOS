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
#include "applib/ui/action_menu_hierarchy.h"
#include "applib/ui/dialogs/actionable_dialog.h"
#include "applib/ui/day_picker.h"
#include "applib/ui/dialogs/dialog.h"
#include "applib/ui/dialogs/expandable_dialog.h"
#include "applib/ui/dialogs/simple_dialog.h"
#include "applib/ui/option_menu_window.h"
#include "applib/ui/time_selection_window.h"
#include "applib/ui/window.h"
#include "apps/system/settings/option_menu.h"
#include "applib/app_exit_reason.h"

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

void option_menu_set_highlight_colors(OptionMenu *option_menu, GColor background, GColor foreground) {
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

// --- confirmation / first-run dialogs ---------------------------------------
void dialog_set_text(Dialog *dialog, const char *text) {
  (void)dialog;
  (void)text;
}

void dialog_set_background_color(Dialog *dialog, GColor background_color) {
  (void)dialog;
  (void)background_color;
}

void dialog_set_icon(Dialog *dialog, uint32_t icon_id) {
  (void)dialog;
  (void)icon_id;
}

void dialog_set_timeout(Dialog *dialog, uint32_t timeout) {
  (void)dialog;
  (void)timeout;
}

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

// ponytail: Notifications' Clear All confirmation and card action menus are
// inert. Upgrade by compiling the actionable/simple dialog family,
// action_menu_hierarchy.c, action_menu_window.c, and timeline_actions.c.
ActionableDialog *actionable_dialog_create(const char *dialog_name) {
  (void)dialog_name;
  return NULL;
}

void actionable_dialog_set_click_config_provider(ActionableDialog *dialog,
                                                 ClickConfigProvider provider) {
  (void)dialog;
  (void)provider;
}

void actionable_dialog_set_action_bar_type(ActionableDialog *dialog,
                                           DialogActionBarType action_bar_type,
                                           ActionBarLayer *action_bar) {
  (void)dialog;
  (void)action_bar_type;
  (void)action_bar;
}

Dialog *actionable_dialog_get_dialog(ActionableDialog *dialog) {
  (void)dialog;
  return NULL;
}

void app_actionable_dialog_push(ActionableDialog *dialog) { (void)dialog; }
void actionable_dialog_pop(ActionableDialog *dialog) { (void)dialog; }

void dialog_set_icon_animate_direction(Dialog *dialog, DialogIconAnimationDirection direction) {
  (void)dialog;
  (void)direction;
}

void dialog_set_callbacks(Dialog *dialog, const DialogCallbacks *callbacks, void *context) {
  (void)dialog;
  (void)callbacks;
  (void)context;
}

void dialog_set_text_color(Dialog *dialog, GColor text_color) {
  (void)dialog;
  (void)text_color;
}

void dialog_set_fullscreen(Dialog *dialog, bool fullscreen) {
  (void)dialog;
  (void)fullscreen;
}

void simple_dialog_push(SimpleDialog *dialog, WindowStack *window_stack) {
  (void)dialog;
  (void)window_stack;
}

void app_exit_reason_set(AppExitReason exit_reason) { (void)exit_reason; }

bool action_menu_is_frozen(ActionMenu *action_menu) {
  (void)action_menu;
  return false;
}

void action_menu_close(ActionMenu *action_menu, bool animated) {
  (void)action_menu;
  (void)animated;
}

ActionMenuLevel *action_menu_level_create(uint16_t max_items) {
  (void)max_items;
  return NULL;
}

ActionMenuItem *action_menu_level_add_action(ActionMenuLevel *level, const char *label,
                                             ActionMenuPerformActionCb callback,
                                             void *action_data) {
  (void)level;
  (void)label;
  (void)callback;
  (void)action_data;
  return NULL;
}

ActionMenuItem *action_menu_level_add_child(ActionMenuLevel *level, ActionMenuLevel *child,
                                            const char *label) {
  (void)level;
  (void)child;
  (void)label;
  return NULL;
}
