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

// Real time/date selection windows are compiled now; stubs removed.

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
// Real dialogs (simple/expandable) are compiled now; stubs removed.

