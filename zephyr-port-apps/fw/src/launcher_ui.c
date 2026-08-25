/* SPDX-License-Identifier: Apache-2.0 */

// Real PebbleOS launcher: a Window + MenuLayer populated from the fw app
// registry, navigated by the real click service (ClickManager /
// click_recognizer) exactly as applib/app.c drives it. We reuse the shipping
// menu_layer.c / scroll_layer.c / click.c and the scaffold's applib UI shell
// (window_create, GContext, layer render). Only the pieces the scaffold lacks
// are provided here as thin glue:
//   - a single ClickManager (there is no per-app process here, so no app_state)
//   - the window_* click-config-provider entry points (copied from window.c,
//     routed to that single ClickManager instead of the window_manager)
//   - app_timer_* and the animation/property_animation tween symbols as
//     link satisfiers.
//
// ponytail: menu navigation uses the non-animated selection path
// (menu_layer_set_selected_next(..., animated=false)) so the slide/scroll tween
// engine (property_animation + the animation service) does not need to be
// ported. The real layout, cell drawing, selection, scroll clamping and click
// state machine are all shipping code. Add the tween engine only if the slide
// animation is wanted on hardware.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Zephyr and Pebble both declare sign_extend() with different signatures; load
// Zephyr's under a private name before the Pebble graphics headers (mirrors
// sandbox_graphics_state.h).
#define sign_extend zephyr_sign_extend
#include <zephyr/kernel.h>
#undef sign_extend
#include <zephyr/sys/printk.h>

#include "applib/fonts/fonts.h"
#include "applib/graphics/graphics.h"
#include "applib/graphics/text.h"
#include "applib/ui/click.h"
#include "applib/ui/click_internal.h"
#include "applib/ui/layer.h"
#include "applib/ui/menu_layer.h"
#include "applib/ui/window.h"

#include "kernel/events.h"
#include "pbl/drivers/button_id.h"
#include "pbl/drivers/task_watchdog.h"
#include "pbl/services/event_service.h"

#include "process_management/pebble_process_md.h"

#include "app_registry.h"
#include "button_input.h"
#include "launcher_ui.h"
#include "sandbox_launcher.h"
#include "system_app.h"

// Provided by input_service.c (the button/click-service agent): the single
// ClickManager, and the button-event -> click_recognizer routing. Our launcher
// window reconfigures this same manager through its ClickConfigProvider.
ClickManager *app_state_get_click_manager(void);

// Provided by sandbox_launcher.c / the applib UI shell (watchface port.c).
void fw_sandbox_display_init(void);
void watchface_port_push_frame(void);
GContext *app_state_get_graphics_context(void);

// ---------------------------------------------------------------------------
// Tiny window stack. The real window_stack.c pulls in the compositor, window
// manager and transitions which are irrelevant to button navigation, so we
// keep a fixed array here and reuse the real Window struct + click glue.
// ponytail: fixed depth 4, deepen if the menu ever pushes more than one child.
// ---------------------------------------------------------------------------
#define STACK_MAX 4
static Window *s_stack[STACK_MAX];
static int s_stack_top = -1;

static Window *prv_top_window(void) {
  return (s_stack_top >= 0) ? s_stack[s_stack_top] : NULL;
}

// ---------------------------------------------------------------------------
// window.c click-config-provider entry points, routed to s_click_manager.
// These mirror the shipping window.c bodies but drop the window_manager and
// recognizer_manager indirection (there is a single visible window here).
// ---------------------------------------------------------------------------
void window_call_click_config_provider(Window *window, void *context) {
  if (!window || !window->click_config_provider) {
    return;
  }
  window->in_click_config_provider = true;
  window->click_config_provider(context);
  window->in_click_config_provider = false;
}

static void prv_apply_click_config(Window *window) {
  ClickManager *mgr = app_state_get_click_manager();
  click_manager_clear(mgr);
  if (!window) {
    return;
  }
  void *context = window->click_config_context ? window->click_config_context : window;
  for (ButtonId button_id = 0; button_id < NUM_BUTTONS; ++button_id) {
    mgr->recognizers[button_id].config.context = context;
  }
  window_call_click_config_provider(window, context);
}

void window_set_click_config_provider_with_context(Window *window,
                                                   ClickConfigProvider provider,
                                                   void *context) {
  if (!window) {
    return;
  }
  window->click_config_provider = provider;
  window->click_config_context = context;
  if (window->on_screen) {
    prv_apply_click_config(window);
  } else {
    window->is_waiting_for_click_config = true;
  }
}

void window_set_click_config_provider(Window *window, ClickConfigProvider provider) {
  window_set_click_config_provider_with_context(window, provider, NULL);
}

ClickConfigProvider window_get_click_config_provider(const Window *window) {
  return window ? window->click_config_provider : NULL;
}

void window_set_click_context(ButtonId button_id, void *context) {
  app_state_get_click_manager()->recognizers[button_id].config.context = context;
}

void window_single_click_subscribe(ButtonId button_id, ClickHandler handler) {
  ClickConfig *cfg = &app_state_get_click_manager()->recognizers[button_id].config;
  cfg->click.repeat_interval_ms = 0;
  cfg->click.handler = handler;
}

void window_single_repeating_click_subscribe(ButtonId button_id,
                                             uint16_t repeat_interval_ms,
                                             ClickHandler handler) {
  if (button_id == BUTTON_ID_BACK) {
    return;
  }
  ClickConfig *cfg = &app_state_get_click_manager()->recognizers[button_id].config;
  cfg->click.repeat_interval_ms = repeat_interval_ms;
  cfg->click.handler = handler;
}

void window_long_click_subscribe(ButtonId button_id, uint16_t delay_ms,
                                 ClickHandler down_handler, ClickHandler up_handler) {
  ClickConfig *cfg = &app_state_get_click_manager()->recognizers[button_id].config;
  cfg->long_click.delay_ms = (delay_ms == 0) ? 500 : delay_ms;
  cfg->long_click.handler = down_handler;
  cfg->long_click.release_handler = up_handler;
}

void window_multi_click_subscribe(ButtonId button_id, uint8_t min_clicks, uint8_t max_clicks,
                                  uint16_t timeout, bool last_click_only, ClickHandler handler) {
  ClickConfig *cfg = &app_state_get_click_manager()->recognizers[button_id].config;
  cfg->multi_click.min = (min_clicks == 0) ? 2 : min_clicks;
  cfg->multi_click.max = (max_clicks == 0) ? min_clicks : max_clicks;
  cfg->multi_click.timeout = (timeout == 0) ? 300 : timeout;
  cfg->multi_click.last_click_only = last_click_only;
  cfg->multi_click.handler = handler;
}

void window_raw_click_subscribe(ButtonId button_id, ClickHandler down_handler,
                                ClickHandler up_handler, void *context) {
  ClickConfig *cfg = &app_state_get_click_manager()->recognizers[button_id].config;
  cfg->raw.up_handler = up_handler;
  cfg->raw.down_handler = down_handler;
  cfg->raw.context = context;
}

// ---------------------------------------------------------------------------
// The launcher menu.
// ---------------------------------------------------------------------------
static Window *s_launcher_window;
static MenuLayer s_menu;
// Once a SANDBOXED app is launched it owns the panel; stop rendering the launcher
// over it. Privileged system apps (fw_system_app_launch) instead ride the shared
// window stack and are rendered by the normal pump, so they leave this false.
static bool s_app_launched;
// SELECT stashes the chosen system-app md here; the launcher loop launches it at
// the top level (not nested inside the click callback).
static const PebbleProcessMd *s_pending_md;

static uint16_t prv_get_num_sections(struct MenuLayer *menu_layer, void *context) {
  return 1;
}

static uint16_t prv_get_num_rows(struct MenuLayer *menu_layer, uint16_t section_index,
                                 void *context) {
  return (uint16_t)fw_app_registry_count();
}

static int16_t prv_get_cell_height(struct MenuLayer *menu_layer, MenuIndex *cell_index,
                                   void *context) {
  return 32;
}

static void prv_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index,
                         void *context) {
  const FwAppRegistryEntry *entry = fw_app_registry_get(cell_index->row);
  if (!entry) {
    return;
  }
  // menu_layer sets the text colour (normal vs highlighted) before this call.
  GFont font = fonts_get_system_font("RESOURCE_ID_GOTHIC_14");
  GRect box = cell_layer->bounds;
  box.origin.x += 4;
  box.size.w -= 4;
  graphics_draw_text(ctx, entry->name, font, box, GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);
}

static void prv_launch_selected(void) {
  MenuIndex index = menu_layer_get_selected_index(&s_menu);
  const FwAppRegistryEntry *entry = fw_app_registry_get(index.row);
  if (!entry) {
    return;
  }
  printk("LAUNCHER_SEL %s\n", entry->name);

  // A registry entry carrying a real PebbleProcessMd is a privileged built-in
  // system app: launch it through the system-app path (deferred to the launcher
  // loop's top level so it is not nested inside this click callback). Entries
  // without an md fall back to the embedded sandboxed PBW, which reinitialises
  // the display and owns the panel afterwards.
  if (entry->md) {
    s_pending_md = entry->md;
    return;
  }
  printk("WINDOW_PUSH %s\n", entry->name);
  s_app_launched = fw_sandbox_launch();
}

// Non-animated navigation handlers (see ponytail note at top of file).
static void prv_menu_up(ClickRecognizerRef recognizer, void *context) {
  printk("LAUNCHER_UP\n");
  menu_layer_set_selected_next(&s_menu, true, MenuRowAlignCenter, false);
}

static void prv_menu_down(ClickRecognizerRef recognizer, void *context) {
  printk("LAUNCHER_DOWN\n");
  menu_layer_set_selected_next(&s_menu, false, MenuRowAlignCenter, false);
}

static void prv_menu_select(ClickRecognizerRef recognizer, void *context) {
  prv_launch_selected();
}

static void prv_launcher_click_config_provider(void *context) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 100, prv_menu_up);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 100, prv_menu_down);
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_menu_select);
}

// ---------------------------------------------------------------------------
// Render + window-stack push/pop.
// ---------------------------------------------------------------------------
static void prv_render_top(void) {
  Window *window = prv_top_window();
  if (!window || s_app_launched) {
    return;
  }
  layer_render_tree(window_get_root_layer(window), app_state_get_graphics_context());
  window->is_render_scheduled = false;
  watchface_port_push_frame();
  fw_fb_dump_uart();
}

static void prv_window_push(Window *window) {
  if (s_stack_top + 1 >= STACK_MAX) {
    return;
  }
  s_stack[++s_stack_top] = window;
  window->on_screen = true;
  printk("WINDOW_PUSH %p depth=%d\n", (void *)window, s_stack_top + 1);
  prv_apply_click_config(window);
  prv_render_top();
}

static void prv_window_pop(void) {
  // The sandbox owns the panel without putting its unprivileged Window on this
  // kernel stack. Treat it as the logical top window so BACK uses the same pop
  // path as a privileged system app.
  if (s_app_launched) {
    fw_sandbox_exit();
    s_app_launched = false;
    printk("WINDOW_POP sandbox depth=%d\n", s_stack_top + 1);
    prv_apply_click_config(prv_top_window());
    prv_render_top();
    return;
  }
  if (s_stack_top < 0) {
    return;
  }
  Window *window = s_stack[s_stack_top--];
  window->on_screen = false;
  printk("WINDOW_POP %p depth=%d\n", (void *)window, s_stack_top + 1);

  // Run the real window's disappear + unload handlers (mirrors shipping window
  // stack pop) so a system app's window frees its data / deinits its menu. This
  // is what makes nested settings submenus pop cleanly back to the parent menu.
  // Must be the last use of `window` — unload may free it.
  if (window->window_handlers.disappear) {
    window->window_handlers.disappear(window);
  }
  if (window->window_handlers.unload) {
    window->window_handlers.unload(window);
  }

  prv_apply_click_config(prv_top_window());
  prv_render_top();
}

// Exposed to the system-app launch core (system_app.c) so a privileged app's
// window rides the same window stack + pump as the launcher.
void fw_window_stack_push(Window *window) { prv_window_push(window); }

int fw_window_stack_depth(void) { return s_stack_top + 1; }

static void prv_launcher_setup(void) {
  // input_service_init() (called from main.c) already brought up the shared
  // ClickManager; here we bring up the panel and let the launcher window's
  // ClickConfigProvider reconfigure that manager for menu navigation.
  fw_sandbox_display_init();

  s_launcher_window = window_create();
  window_set_background_color(s_launcher_window, GColorWhite);

  GRect bounds = window_get_root_layer(s_launcher_window)->bounds;
  menu_layer_init(&s_menu, &bounds);
  menu_layer_set_callbacks(&s_menu, &s_menu, &(MenuLayerCallbacks){
    .get_num_sections = prv_get_num_sections,
    .get_num_rows = prv_get_num_rows,
    .get_cell_height = prv_get_cell_height,
    .draw_row = prv_draw_row,
  });
  layer_add_child(window_get_root_layer(s_launcher_window), menu_layer_get_layer(&s_menu));
  window_set_click_config_provider_with_context(s_launcher_window,
                                                prv_launcher_click_config_provider, &s_menu);

  prv_window_push(s_launcher_window);
  printk("LAUNCHER_UP_READY apps=%u\n", (unsigned)fw_app_registry_count());
}

// ---------------------------------------------------------------------------
// Optional synthetic input source for headless verification. Feeds the REAL
// event queue / click service, so it is a drop-in for the button driver: the
// obelix button driver emits the same PEBBLE_BUTTON_DOWN/UP events.
// ---------------------------------------------------------------------------
#ifdef FW_LAUNCHER_SELFTEST
#include <zephyr/kernel.h>

static void prv_inject_button(ButtonId button_id) {
  PebbleEvent down = { .type = PEBBLE_BUTTON_DOWN_EVENT, .button = { .button_id = button_id } };
  event_put(&down);
  PebbleEvent up = { .type = PEBBLE_BUTTON_UP_EVENT, .button = { .button_id = button_id } };
  event_put(&up);
}

static void prv_selftest_thread(void *a, void *b, void *c) {
  k_msleep(3000);
  printk("LAUNCHER_SELFTEST begin\n");
  // Registry menu order: TicToc(0), Kickstart(1), Watch Only(2), Settings(3).
  // Walk down to Settings and launch it so the host can screenshot it. Long
  // pauses so the framebuffer dump for each frame drains the UART first.
  prv_inject_button(BUTTON_ID_DOWN);
  k_msleep(1500);
  prv_inject_button(BUTTON_ID_DOWN);
  k_msleep(1500);
  prv_inject_button(BUTTON_ID_DOWN);
  k_msleep(1500);
  prv_inject_button(BUTTON_ID_SELECT);
  printk("LAUNCHER_SELFTEST end\n");
}

K_THREAD_STACK_DEFINE(s_selftest_stack, 2048);
static struct k_thread s_selftest_thread;

static void prv_start_selftest(void) {
  k_thread_create(&s_selftest_thread, s_selftest_stack, K_THREAD_STACK_SIZEOF(s_selftest_stack),
                  prv_selftest_thread, NULL, NULL, NULL, K_PRIO_PREEMPT(10), 0, K_NO_WAIT);
}
#else
static void prv_start_selftest(void) {}
#endif

// ---------------------------------------------------------------------------
// KernelMain UI event loop. Mirrors launcher_main_loop() but also routes button
// events through the real click service (like applib/app.c) and renders.
// ---------------------------------------------------------------------------
// One iteration of the shared UI pump: take an event, drive BACK / the click
// service / deferred callbacks, dispatch to event-service clients, and render the
// top window. Shared by the launcher loop and system_app.c's app_event_loop so a
// launched app runs on exactly the same loop the launcher does.
void fw_ui_pump_once(void) {
  // KernelMain is in the task-watchdog mask; kick its bit every pump so the HW
  // WDT stays fed while this loop runs. event_take_timeout blocks at most 1s, so
  // the bit refreshes at least once per second even with no events; a genuinely
  // hung loop stops kicking and the watchdog correctly resets.
  task_watchdog_bit_set(PebbleTask_KernelMain);

  PebbleEvent event;
  if (!event_take_timeout(&event, 1000)) {
    return;
  }

  switch (event.type) {
    case PEBBLE_BUTTON_DOWN_EVENT:
      // BACK pops the window stack (unless we're at the root), mirroring the
      // default back-button behaviour in applib/app.c. A sandbox has no safe
      // kernel-context click handlers, so its other buttons remain unhandled;
      // otherwise events drive the current window's click recognizers.
      if (event.button.button_id == BUTTON_ID_BACK &&
          (s_app_launched || s_stack_top > 0)) {
        prv_window_pop();
      } else if (!s_app_launched) {
        input_service_handle_button_event(&event);
      }
      break;
    case PEBBLE_BUTTON_UP_EVENT:
      if (!s_app_launched) {
        input_service_handle_button_event(&event);
      }
      break;
    case PEBBLE_CALLBACK_EVENT:
      // click.c repeat/long/multi callbacks land here via app_timer.
      if (event.callback.callback) {
        event.callback.callback(event.callback.data);
      }
      break;
    default:
      break;
  }

  event_service_handle_event(&event);
  event_cleanup(&event);

  // Reflect any selection/window change onto the panel.
  prv_render_top();
}

void fw_launcher_ui_run(void) {
  extern void pebble_zephyr_core_event_loop_init(void);
  pebble_zephyr_core_event_loop_init();

  prv_launcher_setup();
  prv_start_selftest();

  while (true) {
    fw_ui_pump_once();

    // SELECT on a system-app entry stashed its md; launch it here at the loop's
    // top level. fw_system_app_launch runs the app on this same KernelMain loop
    // and returns when the app exits (BACK past its root window).
    if (s_pending_md) {
      const PebbleProcessMd *md = s_pending_md;
      s_pending_md = NULL;
      fw_system_app_launch(md);
    }
  }
}
