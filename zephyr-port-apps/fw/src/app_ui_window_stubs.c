/* SPDX-License-Identifier: Apache-2.0 */

// Inert stubs for the applib helper windows the Alarms app reaches only on user
// interaction (new-alarm editor time/day pickers, alarm-detail action menu and
// option menus, and the confirmation / first-run dialogs). The Alarms app
// launches straight into its real alarm-list menu (backed by the real alarm
// service), which renders with the real menu_layer + menu_cell_layer; these
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

// action_menu_window.c is now compiled; its real functions are used.

// Real dialogs (simple/expandable) are compiled now; stubs removed.

