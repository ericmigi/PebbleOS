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
#include <stdio.h>

// Zephyr and Pebble both declare sign_extend() with different signatures; load
// Zephyr's under a private name before the Pebble graphics headers (mirrors
// sandbox_graphics_state.h).
#define sign_extend zephyr_sign_extend
#include <zephyr/kernel.h>
#undef sign_extend
#include <zephyr/sys/printk.h>

#include "applib/fonts/fonts.h"
#include "applib/graphics/gtypes.h"
#include "applib/graphics/graphics.h"
#include "applib/graphics/text.h"
#include "applib/ui/click.h"
#include "applib/ui/click_internal.h"
#include "applib/ui/layer.h"
#include "applib/ui/menu_layer.h"
#include "applib/ui/status_bar_layer.h"
#include "applib/ui/window.h"

#include "kernel/events.h"
#include "pbl/drivers/button_id.h"
#include "pbl/drivers/task_watchdog.h"
#include "pbl/services/event_service.h"

#include "process_management/pebble_process_md.h"

#include "shell/system_theme.h"

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

// Once a SANDBOXED app is launched it owns the panel; stop rendering the launcher
// over it. Privileged system apps (fw_system_app_launch) instead ride the shared
// window stack and are rendered by the normal pump, so they leave this false.
static bool s_app_launched;
// A requested system-app launch (SELECT in a menu, the shell opening the real
// launcher). Processed at the pump's top level, not nested inside callbacks.
static const PebbleProcessMd *s_pending_md;

void fw_shell_request_launch(const PebbleProcessMd *md) { s_pending_md = md; }

bool fw_shell_launch_pending(void) { return s_pending_md != NULL; }

// Shell hooks, overridden by fw_shell.c on boards running the real shell
// (watchface at boot + real launcher app). Defaults preserve the pt2 custom
// launcher-menu behavior.
__attribute__((weak)) bool fw_shell_handle_button_down(ButtonId button_id) {
  (void)button_id;
  return false;
}

__attribute__((weak)) void fw_shell_note_activity(void) {}

__attribute__((weak)) void fw_shell_on_app_exit(const PebbleProcessMd *md) { (void)md; }

// True when BACK should pop the top window even at stack depth 1 (the real
// shell's boot-rooted launcher); defaults to protecting the root window.
__attribute__((weak)) bool fw_shell_back_should_pop(void) { return false; }

// Backlight hooks (qemu_board.c on the real-shell qemu build); shipping's
// kernel drives light_button_pressed/released from every button event.
__attribute__((weak)) void fw_light_button_pressed(void) {}
__attribute__((weak)) void fw_light_button_released(void) {}

// Called after a window is popped, before the revealed window renders; the
// real shell uses it to arm the compositor close transition while the app
// framebuffer still holds the outgoing screen.
__attribute__((weak)) void fw_shell_before_pop_render(Window *window, int new_depth) {
  (void)window;
  (void)new_depth;
}

// compositor_port.c (real-shell builds); pt2 has no compositor, so the pump
// falls back to these no-transition defaults.
__attribute__((weak)) bool fw_compositor_handle_frame(void) { return false; }
__attribute__((weak)) bool fw_compositor_transition_pending(void) { return false; }
__attribute__((weak)) bool fw_compositor_render_blocked(void) { return false; }
__attribute__((weak)) void fw_compositor_launch_frame_exited(int nesting) { (void)nesting; }

#ifndef FW_REAL_SHELL
// ---------------------------------------------------------------------------
// The launcher menu.
// ---------------------------------------------------------------------------
static Window *s_launcher_window;
static MenuLayer s_menu;

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

// Per-row app icon, drawn left of the name like shipping's launcher glances.
// Map each app to an embedded icon resource (served by port.c through
// sys_resource_*); apps we have no dedicated icon for fall back to the generic
// watch-app glyph. See launcher_ui.h for the FW_RES_ICON_* IDs.
#define LAUNCHER_ICON_SIZE 25
#define LAUNCHER_ICON_LEFT 6
#define LAUNCHER_TEXT_LEFT (LAUNCHER_ICON_LEFT + LAUNCHER_ICON_SIZE + 5)

static uint32_t prv_icon_res_for_app(const FwAppRegistryEntry *entry) {
  switch (entry->install_id) {
    case -7: return FW_RES_ICON_SETTINGS;
    case -3: return FW_RES_ICON_MUSIC;
    case -4: return FW_RES_ICON_NOTIFICATIONS;
    case -5: return FW_RES_ICON_ALARMS;
    case -6: return FW_RES_ICON_WATCHFACES;
    default: return FW_RES_ICON_GENERIC;
  }
}

// Lazily decode + cache each icon once (keyed by resource id). PNG decode on
// every render would be wasteful; the cache keeps redraws cheap.
// ponytail: fixed small cache; the launcher only uses a handful of distinct icons.
#define ICON_CACHE_MAX 8
static struct { uint32_t id; GBitmap bmp; bool valid; } s_icon_cache[ICON_CACHE_MAX];

static GBitmap *prv_get_icon(uint32_t resource_id) {
  for (int i = 0; i < ICON_CACHE_MAX; ++i) {
    if (s_icon_cache[i].valid && s_icon_cache[i].id == resource_id) {
      return &s_icon_cache[i].bmp;
    }
  }
  for (int i = 0; i < ICON_CACHE_MAX; ++i) {
    if (!s_icon_cache[i].valid) {
      if (!gbitmap_init_with_resource(&s_icon_cache[i].bmp, resource_id)) {
        return NULL;
      }
      s_icon_cache[i].id = resource_id;
      s_icon_cache[i].valid = true;
      return &s_icon_cache[i].bmp;
    }
  }
  return NULL;
}

static void prv_draw_icon(GContext *ctx, const GRect *cell_bounds, uint32_t resource_id) {
  GBitmap *icon = prv_get_icon(resource_id);
  if (!icon) {
    return;
  }
  const GRect src = icon->bounds;
  GRect dst = {
    .origin = { cell_bounds->origin.x + LAUNCHER_ICON_LEFT,
                cell_bounds->origin.y + (cell_bounds->size.h - src.size.h) / 2 },
    .size = src.size,
  };
  // Icons are black-on-transparent PNGs; GCompOpSet honours the alpha so the
  // cell background (white / highlight) shows through.
  graphics_context_set_compositing_mode(ctx, GCompOpSet);
  graphics_draw_bitmap_in_rect(ctx, icon, &dst);
}

static void prv_draw_row(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index,
                         void *context) {
  const FwAppRegistryEntry *entry = fw_app_registry_get(cell_index->row);
  if (!entry) {
    return;
  }
  prv_draw_icon(ctx, &cell_layer->bounds, prv_icon_res_for_app(entry));

  // menu_layer sets the text colour (normal vs highlighted) before this call.
  // Launcher rows are menu-cell titles: use the shipping MenuCellTitle face
  // (GOTHIC_24_BOLD at the port's Large content size) instead of the fallback.
  GFont font = system_theme_get_font_for_default_size(TextStyleFont_MenuCellTitle);
  GRect box = cell_layer->bounds;
  box.origin.x += LAUNCHER_TEXT_LEFT;
  box.size.w -= LAUNCHER_TEXT_LEFT;
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
#endif  // !FW_REAL_SHELL

// ---------------------------------------------------------------------------
// In-app window push/pop slide transition, mirroring shipping
// window_stack_animation_rect.c: the incoming window's root-layer frame tweens
// on the moook curve from +DISP_COLS (push) / -DISP_COLS (pop) to 0. Only the
// incoming window animates; the outgoing window's pixels stay in the
// framebuffer between frames (it never moves in shipping either), and
// patch-trace fills the overshoot trail like the reference render path.
// Enabled for the real-shell build only (pt2's scaffold shell keeps
// instant push/pop).
// ---------------------------------------------------------------------------
#ifdef FW_REAL_SHELL
#include "applib/graphics/graphics_private.h"
#include "applib/ui/animation_interpolate.h"
#include "applib/ui/property_animation.h"

// window_private.h's entry, defined in watchface_sandboxed/src/port.c.
void window_schedule_render(Window *window);

static struct {
  Window *moving;
  int16_t last_x;
} s_win_trans;

static bool s_next_push_animated;

void fw_window_stack_set_next_push_animated(bool animated) {
  s_next_push_animated = animated;
}

static void prv_win_trans_frame_setter(void *subject, GRect rect) {
  Window *window = subject;
  // Repeated curve samples inside one moook frame must not emit duplicate
  // display frames (layer_set_frame no-ops on an unchanged frame in shipping).
  if (grect_equal(&rect, &window->layer.frame)) {
    return;
  }
  layer_set_frame(&window->layer, &rect);
  window_schedule_render(window);
}

static void prv_win_trans_update(Animation *a, const AnimationProgress progress) {
  property_animation_update_grect((PropertyAnimation *)a, progress);
}

static void prv_win_trans_teardown(Animation *a) {
  s_win_trans.moving = NULL;
  animation_destroy(a);
}

static void prv_win_trans_start(Window *window, bool from_left) {
  static const PropertyAnimationImplementation s_impl = {
    .base = {
      .update = prv_win_trans_update,
      .teardown = prv_win_trans_teardown,
    },
    .accessors = { .setter.grect = prv_win_trans_frame_setter },
  };
  const GRect end = window->layer.frame;
  GRect start = end;
  start.origin.x += from_left ? -DISP_COLS : DISP_COLS;
  PropertyAnimation *pa = property_animation_create(&s_impl, window, NULL, NULL);
  if (!pa) {
    return;
  }
  property_animation_set_from_grect(pa, &start);
  property_animation_set_to_grect(pa, &end);
  Animation *anim = property_animation_get_animation(pa);
  animation_set_custom_interpolation(anim, interpolate_moook);
  animation_set_duration(anim, interpolate_moook_duration());
  s_win_trans.moving = window;
  s_win_trans.last_x = INT16_MAX;
  // Park the incoming window offscreen; marking it dirty makes the pump render
  // it there once (the reference's first transition frame repeats the old
  // screen). Later curve samples only render when the frame actually moves.
  layer_set_frame(&window->layer, &start);
  window_schedule_render(window);
  animation_schedule(anim);
}

// In-app transitions only run between two windows of the running app (shipping
// gives each app its own window stack; the app's first push and last pop are
// covered by the compositor instead).
static bool prv_win_trans_should_animate(int depth_above_pop) {
  return depth_above_pop > fw_system_app_base_depth() &&
         !fw_compositor_transition_pending();
}
#else
static bool s_next_push_animated;
void fw_window_stack_set_next_push_animated(bool animated) { (void)animated; }
#endif  // FW_REAL_SHELL

// ---------------------------------------------------------------------------
// Render + window-stack push/pop.
// ---------------------------------------------------------------------------
// Render + push only when something marked the top window dirty
// (layer_mark_dirty -> window_schedule_render), like the shipping render loop.
// Unconditional pushes emit duplicate display frames on every event (per-second
// tick spam, double frames during animations) that the reference never shows.
static void prv_render_top(void) {
  Window *window = prv_top_window();
  if (!window || s_app_launched || !window->is_render_scheduled ||
      fw_compositor_render_blocked()) {
    return;
  }
  extern uint64_t rtc_get_ticks(void);
  const uint32_t frame_start_ms = (uint32_t)rtc_get_ticks();
  layer_render_tree(window_get_root_layer(window), app_state_get_graphics_context());
#ifdef FW_REAL_SHELL
  if (s_win_trans.moving == window) {
    graphics_patch_trace_of_moving_rect(app_state_get_graphics_context(),
                                        &s_win_trans.last_x, window->layer.frame);
  }
#endif
  window->is_render_scheduled = false;
  // A pending/running compositor transition owns the panel: it composites the
  // app framebuffer itself and pushes the system framebuffer per frame.
  if (fw_compositor_handle_frame()) {
    return;
  }
  watchface_port_push_frame();
  // Shipping app frames traverse app FB -> compositor copy -> display before
  // KernelMain pumps the next event (~29-31 ms/frame under this host's TCG);
  // the port's direct render+push is ~10 ms cheaper and jitters, and the
  // animation rate control is latency-bound for the first ~8 frames of every
  // animation, so unpadded frames sample the curves earlier than the reference
  // and the streams diverge. Pace app frames on an absolute 30 ms grid
  // (drift-free: each deadline advances from the previous one, resyncing after
  // idle gaps) so curve sampling is deterministic instead of riding the
  // render-cost jitter.
  // ponytail: 30 ms matches the ref pipeline's measured cadence; the ref
  // itself flips animation frames that land within ~2 ms of curve boundaries
  // run-to-run, which no port-side pacing can track.
#if defined(CONFIG_BOARD_QEMU_EMERY)
  // QEMU-only determinism aid; hardware gets its cadence from the real
  // display pipeline latency, like shipping.
  static uint32_t s_frame_deadline_ms;
  const uint32_t now_ms = (uint32_t)rtc_get_ticks();
  if ((int32_t)(frame_start_ms - s_frame_deadline_ms) > 20) {
    // Idle gap: re-anchor on this frame's animation-clock bucket edge so the
    // animation callback samples elapsed as exact cadence multiples.
    s_frame_deadline_ms = frame_start_ms - (frame_start_ms % 10u);
  }
  // 29 ms matches the reference's cadence under the frame_walk `-icount
  // shift=3` harness, where both firmwares are bit-deterministic.
  s_frame_deadline_ms += 29;
  if ((int32_t)(s_frame_deadline_ms - now_ms) > 0) {
    k_sleep(K_MSEC(s_frame_deadline_ms - now_ms));
  }
#endif
  fw_fb_dump_uart();
}

// window_private.h's entry, defined in watchface_sandboxed/src/port.c.
void window_schedule_render(Window *window);

static void prv_window_push(Window *window) {
  if (s_stack_top + 1 >= STACK_MAX) {
    return;
  }
  const bool animated = s_next_push_animated;
  s_next_push_animated = false;
  s_stack[++s_stack_top] = window;
  window->on_screen = true;
  printk("WINDOW_PUSH %p depth=%d\n", (void *)window, s_stack_top + 1);
  prv_apply_click_config(window);
#ifdef FW_REAL_SHELL
  if (animated && prv_win_trans_should_animate(s_stack_top)) {
    prv_win_trans_start(window, false /* from the right */);
    return;
  }
#else
  (void)animated;
#endif
  window_schedule_render(window);
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
    window_schedule_render(prv_top_window());
    prv_render_top();
    return;
  }
  if (s_stack_top < 0) {
    return;
  }
  Window *window = s_stack[s_stack_top--];
  window->on_screen = false;
  printk("WINDOW_POP %p depth=%d\n", (void *)window, s_stack_top + 1);
  fw_shell_before_pop_render(window, s_stack_top + 1);

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
#ifdef FW_REAL_SHELL
  if (prv_top_window() && prv_win_trans_should_animate(s_stack_top + 1)) {
    prv_win_trans_start(prv_top_window(), true /* from the left */);
    return;
  }
#endif
  window_schedule_render(prv_top_window());
  // A pending close transition means this pop leaves the exiting app; the
  // revealed window must render from its own app context (user_data still
  // points at the exiting app here), so defer to the owning pump's render.
  if (!fw_compositor_transition_pending()) {
    prv_render_top();
  }
}

// Exposed to the system-app launch core (system_app.c) so a privileged app's
// window rides the same window stack + pump as the launcher.
void fw_window_stack_push(Window *window) { prv_window_push(window); }

void fw_window_stack_pop(void) { prv_window_pop(); }

int fw_window_stack_depth(void) { return s_stack_top + 1; }

Window *fw_window_stack_top(void) { return prv_top_window(); }

#ifndef FW_REAL_SHELL
// ---------------------------------------------------------------------------
// Launcher status bar (top strip): BT status glyph on the left, battery percent
// + battery glyph on the right, mirroring shipping's launcher chrome. The applib
// StatusBarLayer only renders a clock/title, so the battery + BT indicators are
// drawn here directly. STATUS_BAR_LAYER_HEIGHT keeps the strip height matched to
// the platform's real status bar.
// ---------------------------------------------------------------------------
static Layer s_status_layer;

// ponytail: this recovery FW has no battery or BLE service wired, so the battery
// reads a full placeholder and BT is drawn disconnected (which is accurate here).
// Point these at battery_state_service_peek() / connection_service_peek() once
// those services are ported.
static uint8_t prv_battery_percent(void) { return 100; }
static bool prv_bt_connected(void) { return false; }

static void prv_status_update_proc(Layer *layer, GContext *ctx) {
  const GRect b = layer->bounds;
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, &b);

  // BT glyph (16x16) at the left, vertically centred. Only the disconnected
  // glyph is embedded (accurate for this recovery FW); embed the connected one
  // and switch on prv_bt_connected() when BLE is wired.
  (void)prv_bt_connected;
  GBitmap *bt = prv_get_icon(FW_RES_ICON_BT_DISCONNECTED);
  if (bt) {
    GRect dst = { .origin = { b.origin.x + 3,
                              b.origin.y + (b.size.h - bt->bounds.size.h) / 2 },
                  .size = bt->bounds.size };
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    graphics_draw_bitmap_in_rect(ctx, bt, &dst);
  }

  // Battery glyph on the right: outline body + terminal nub + proportional fill.
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_fill_color(ctx, GColorBlack);
  const int16_t body_w = 20, body_h = 10;
  const int16_t bx = b.origin.x + b.size.w - 3 - body_w;
  const int16_t by = b.origin.y + (b.size.h - body_h) / 2;
  GRect body = { { bx, by }, { body_w, body_h } };
  graphics_draw_rect(ctx, &body);
  GRect nub = { { bx + body_w, by + 3 }, { 2, body_h - 6 } };
  graphics_fill_rect(ctx, &nub);
  const uint8_t pct = prv_battery_percent();
  const int16_t fill_w = ((body_w - 4) * pct) / 100;
  if (fill_w > 0) {
    GRect fill = { { bx + 2, by + 2 }, { fill_w, body_h - 4 } };
    graphics_fill_rect(ctx, &fill);
  }

  // Percentage text, right-aligned just left of the battery glyph.
  char buf[8];
  snprintf(buf, sizeof(buf), "%u%%", pct);
  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  graphics_context_set_text_color(ctx, GColorBlack);
  GRect txt = { { b.origin.x, b.origin.y - 2 }, { bx - 3, b.size.h } };
  graphics_draw_text(ctx, buf, font, txt, GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentRight, NULL);
}

static void prv_launcher_setup(void) {
  // input_service_init() (called from main.c) already brought up the shared
  // ClickManager; here we bring up the panel and let the launcher window's
  // ClickConfigProvider reconfigure that manager for menu navigation.
  fw_sandbox_display_init();

  s_launcher_window = window_create();
  window_set_background_color(s_launcher_window, GColorWhite);

  Layer *root = window_get_root_layer(s_launcher_window);
  GRect bounds = root->bounds;

  // Reserve the top strip for the status bar; the menu fills the rest.
  const int16_t status_h = STATUS_BAR_LAYER_HEIGHT;
  GRect status_frame = { bounds.origin, { bounds.size.w, status_h } };
  layer_init(&s_status_layer, &status_frame);
  layer_set_update_proc(&s_status_layer, prv_status_update_proc);

  GRect menu_frame = { { bounds.origin.x, bounds.origin.y + status_h },
                       { bounds.size.w, bounds.size.h - status_h } };
  menu_layer_init(&s_menu, &menu_frame);
  menu_layer_set_callbacks(&s_menu, &s_menu, &(MenuLayerCallbacks){
    .get_num_sections = prv_get_num_sections,
    .get_num_rows = prv_get_num_rows,
    .get_cell_height = prv_get_cell_height,
    .draw_row = prv_draw_row,
  });
  layer_add_child(root, menu_layer_get_layer(&s_menu));
  layer_add_child(root, &s_status_layer);
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
#endif  // !FW_REAL_SHELL

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
      fw_shell_note_activity();
    case PEBBLE_BUTTON_UP_EVENT: {
      // Align button handling to the 10 ms animation-clock bucket edge (see
      // sys_get_ticks in port.c): animations the handler schedules then start
      // at a fixed clock phase, so their frames sample deterministic elapsed
      // times instead of depending on where the keypress fell in the bucket.
      extern uint64_t rtc_get_ticks(void);
      const uint32_t phase_ms = (uint32_t)rtc_get_ticks() % 10u;
      if (phase_ms) {
        k_sleep(K_MSEC(10u - phase_ms));
      }
      break;
    }
    default:
      break;
  }

  switch (event.type) {
    case PEBBLE_BUTTON_DOWN_EVENT:
      fw_light_button_pressed();
      // BACK pops the window stack (unless we're at the root), mirroring the
      // default back-button behaviour in applib/app.c. A sandbox has no safe
      // kernel-context click handlers, so its other buttons remain unhandled;
      // otherwise events drive the current window's click recognizers.
      if (event.button.button_id == BUTTON_ID_BACK &&
          (s_app_launched || s_stack_top > 0 || fw_shell_back_should_pop() ||
           fw_system_app_launch_nesting() >= 2)) {
        // Any launched-on-top app (nesting >= 2: launcher, Settings, ...)
        // exits through its root pop; app_event_loop returns and the shell
        // chains the next launch (relaunch launcher / watchface).
        prv_window_pop();
      } else if (!s_app_launched &&
                 !fw_shell_handle_button_down(event.button.button_id)) {
        input_service_handle_button_event(&event);
      }
      break;
    case PEBBLE_BUTTON_UP_EVENT:
      fw_light_button_released();
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

  // Requested launches run here, at the pump's top level (never nested inside a
  // click/render callback). fw_system_app_launch pumps this same loop until the
  // app exits; fw_shell_on_app_exit may then chain another launch (e.g. return
  // to the launcher after an app launched from it exits).
  while (s_pending_md) {
    const PebbleProcessMd *md = s_pending_md;
    s_pending_md = NULL;
    fw_system_app_launch(md);
    fw_shell_on_app_exit(md);
    prv_render_top();
  }
}

#ifndef FW_REAL_SHELL
void fw_launcher_ui_run(void) {
  extern void pebble_zephyr_core_event_loop_init(void);
  pebble_zephyr_core_event_loop_init();

  prv_launcher_setup();
  prv_start_selftest();

  while (true) {
    fw_ui_pump_once();
  }
}
#endif  // !FW_REAL_SHELL
