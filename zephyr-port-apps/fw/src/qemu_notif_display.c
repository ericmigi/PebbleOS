/* SPDX-License-Identifier: Apache-2.0 */

// Notification display on the Zephyr port. The QEMU receive thread
// (qemu_notif_rx.c) decodes an injected notification and calls
// fw_notification_show (thread-safe stash only). fw_notification_poll() runs on
// KernelMain from the shared UI pump (fw_ui_pump_once) and renders the pending
// notification via the shipping dialog UI — same context that owns the window
// stack, so no cross-thread window push.
//
// ponytail: a dialog, not the full modal notification_window (peek_layer +
// timeline layout + action menu). Needs modal_manager to overlay arbitrary
// apps; today it pushes onto the shared window stack. Swap in the real
// notification_window once modal_manager + the timeline layout are ported.

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <stdio.h>
#include <string.h>

#include "applib/graphics/gtypes.h"
#include "applib/ui/dialogs/dialog.h"
#include "applib/ui/dialogs/simple_dialog.h"

static char s_title[64];
static char s_subtitle[64];
static char s_body[128];
static char s_text[256];
static volatile bool s_pending;

void fw_notification_show(const char *title, const char *subtitle, const char *body) {
  strncpy(s_title, title ? title : "", sizeof(s_title) - 1);
  s_title[sizeof(s_title) - 1] = '\0';
  strncpy(s_subtitle, subtitle ? subtitle : "", sizeof(s_subtitle) - 1);
  s_subtitle[sizeof(s_subtitle) - 1] = '\0';
  strncpy(s_body, body ? body : "", sizeof(s_body) - 1);
  s_body[sizeof(s_body) - 1] = '\0';
  s_pending = true;
}

// Called from fw_ui_pump_once (KernelMain). Renders one pending notification.
void fw_notification_poll(void) {
  if (!s_pending) {
    return;
  }
  s_pending = false;
  if (s_subtitle[0]) {
    snprintf(s_text, sizeof(s_text), "%s\n%s\n%s", s_title, s_subtitle, s_body);
  } else {
    snprintf(s_text, sizeof(s_text), "%s\n%s", s_title, s_body);
  }
  SimpleDialog *sd = simple_dialog_create("Notification");
  if (!sd) {
    return;
  }
  Dialog *dialog = simple_dialog_get_dialog(sd);
  dialog_set_text(dialog, s_text);
  dialog_set_background_color(dialog, GColorWhite);
  app_simple_dialog_push(sd);
  printk("NOTIF_SHOWN \"%s\"\n", s_title);
}
