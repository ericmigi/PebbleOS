# System-apps buildout (Zephyr port, `fw` app)

> This file is additive. Each contributor uses its own `##` section — do not
> rewrite others' sections.

## Privileged system-app launch path + registration hook

_Owner: system-app launch-core work. Status: TicToc (default analog watchface)
launches from the launcher through its real `PebbleProcessMd` and renders on the
panel; build green on `pt2`._

### What shipping does (reference)

`~/dev/pebbleos`, traced end to end:

- A system app is declared by a `*_get_app_info()` returning a static
  `PebbleProcessMdSystem` (`{ .common = { uuid, main_func, process_type }, name,
  icon_resource_id, run_level }`). `is_unprivileged` is left false ⇒ the app runs
  **privileged**. (`pebble_process_md.h:55/91`; `apps/watch/tictoc/tictoc.c`.)
- The launcher menu posts `app_manager_put_launch_app_event({id, ...})`
  (`apps/system/launcher/default/menu_layer.c:26`). KernelMain
  (`kernel/event_loop.c:400`) turns it into `process_manager_launch_process()` →
  `app_install_get_md(id)` (`app_install_manager.c:831`, negative id ⇒ built-in,
  `md_fn()` returns the static md) → `app_manager_launch_new_app()` →
  `prv_app_start()` (`app_manager.c:250`).
- `prv_app_start` sizes/loads the app, then `pebble_task_create(PebbleTask_App,
  …)` with `prv_app_task_main` (`app_manager.c:355`). `prv_app_task_main`
  (`app_manager.c:125`) runs `app_state_init(); task_init();` drops privilege
  only if `is_unprivileged`, then calls `main_func()`.
- `main_func` (e.g. `tictoc_main`) = `prv_init()` (alloc, `window_init`,
  `app_window_stack_push`, `rtc_get_time_tm`, `tick_timer_service_subscribe`) →
  `app_event_loop()` (`applib/app.c:214`, loops `sys_get_pebble_event` until
  `PEBBLE_PROCESS_DEINIT_EVENT`) → `prv_deinit()`. Exit / BACK-past-root →
  `sys_exit()` → KernelMain closes the app and relaunches the launcher.

### What the port does (this implementation)

The `fw` scaffold already runs the launcher (which shipping models as an app) on
the single KernelMain UI loop, with a real window stack, click service, GContext
and framebuffer push. So a privileged system app is launched **inline on
KernelMain** — the launcher is just "the app at window-stack depth 0":

- `fw_system_app_launch(md)` (`fw/src/system_app.c`) is the port's analog of the
  `prv_app_start`/`prv_app_task_main` core: it calls `md->main_func()` privileged
  (no MPU sandbox, unlike `fw_sandbox_launch()`), then returns to the launcher.
- The port `app_event_loop()` (`fw/src/system_app.c`) hands the app's window
  (just pushed via `app_window_stack_push`) to the shared window stack + pump
  (`fw_window_stack_push` / `fw_ui_pump_once`, exposed from `launcher_ui.c`) and
  pumps until BACK pops it (depth returns to the entry depth) — the analog of
  looping until `PEBBLE_PROCESS_DEINIT_EVENT`.
- The launcher (`launcher_ui.c`) on SELECT stashes the entry's md in
  `s_pending_md` and launches it at the loop's top level (not nested in the click
  callback). Entries with no md fall back to the sandboxed PBW (unchanged).

Load-bearing files:
`fw/src/system_app.c`, `fw/src/launcher_ui.c` (`fw_ui_pump_once`,
`fw_window_stack_push`, `fw_window_stack_depth`, `prv_launch_selected`),
`fw/src/app_registry.c` (md table), `watchface_sandboxed/src/port.c`
(`app_window_stack_get_top_window` accessor + `g_fw_privileged_window` load gate).

UART markers: `LAUNCHER_SEL <name>` → `SYS_APP_LAUNCH <name>` → `SYS_APP_LOOP
depth=2` → (BACK) `WINDOW_POP …` → `SYS_APP_EXIT <name>`.

### How to add a system app (the registration hook)

Three edits, no launch-core changes:

1. **Registry** — `fw/src/app_registry.c`: add the app's header include and an
   `md_fn` to the `s_system_apps[]` row (match the real AppInstallId + name):
   ```c
   #include "apps/system/settings/settings.h"
   ...
   { -7, "Settings", settings_get_app_info },   // md_fn non-NULL ⇒ privileged launch
   ```
   A NULL `md_fn` (the default for not-yet-ported rows) keeps the sandbox
   fallback. `fw_app_registry_init` calls `md_fn()` once and stores the md on the
   `FwAppRegistryEntry`; the launcher hands it to `fw_system_app_launch`.

2. **CMake** — `fw/CMakeLists.txt`: add the app's real source(s) to
   `target_sources` under the "Privileged built-in system apps" group, plus any
   new `lib/util/*.c` the app's draw path pulls in (e.g. TicToc needed
   `lib/util/math.c` for `integer_sqrt`). Watch for undefined-reference link
   errors — they name exactly the missing TU.

3. **Per-file compile props** (only if the app trips them):
   - Real app sources that pull both Zephyr (via `FreeRTOS.h`) and Pebble
     graphics/math headers need `-include fw_zephyr_pre.h` (resolves the
     `sign_extend` collision).
   - Generated resource IDs the port lacks: define them via
     `COMPILE_DEFINITIONS "RESOURCE_ID_...=0"` (icons are cosmetic; the launcher
     menu draws names). A real resource pipeline is P3.
   - A function called without a reachable declaration (shipping rode implicit
     decls; the port is `-Werror=implicit-*`): force-include the header, or add
     the decl to the matching `fw/include/**` port stub.

### Known ceilings (ponytail-marked in the code)

- **Runs on KernelMain, not a `PebbleTask_App` task.** Fine for watchfaces and
  simple apps; the launcher already proves applib UI on KernelMain. Apps needing
  true App-task isolation (own heap/stack guard, App-task click timers, real
  `PEBBLE_PROCESS_DEINIT` routing, or applib code that hard-asserts
  `PebbleTask_App`) are the P3 upgrade: give `fw_system_app_launch` a dedicated
  privileged `PebbleTask_App` thread + event forwarding, mirroring
  `fw_sandbox_launch()`'s thread model minus the MPU.
- **`layer_get_unobstructed_bounds`** is replaced with a full-bounds shim
  (`system_app.c`); `applib/ui/layer.c`'s service-backed, `PebbleTask_App`-
  asserting versions are renamed out in CMake. The port has no unobstructed-area
  service (no notification/modal overlay). Add the real service with modals (P3).
- **App heap** is the kernel heap (`app_zalloc_check`→`k_calloc`); shipping
  carves a separate app RAM segment.
- **`util/time/time.h`** is shadowed by a minimal port stub
  (`fw/include/util/time/time.h`) because the shipping header redeclares
  `localtime_r`/`gmtime_r` without Zephyr libc's `restrict` qualifiers. A ported
  app needing the shipping extras (TimezoneInfo, `time_t_to_string`) must
  reconcile those qualifiers in the real header instead.

## Watchfaces picker + Settings shell (menu apps + nested navigation)

_Owner: menu-app fan-out. Status: WATCHFACES picker and the SETTINGS shell (top
menu + all 11 submodule entries) launch from the launcher through their real
`*_get_app_info()` and render on the panel; build green on `pt2`
(FLASH 164952 B, RAM unchanged). Health settings is a real submodule; the other
10 are navigable "Not ported" stubs (real settings window shell, deferred
service backends)._

### What shipping does (reference)

Both are single-`main` `ProcessTypeApp` system apps that push a `Window` holding a
`MenuLayer` and call `app_event_loop()`. Watchfaces (`apps/system/watchfaces.c`)
lists installed watchfaces via `AppMenuDataSource` and sets the active face on
SELECT. Settings (`apps/system/settings/settings.c`) is an umbrella: its top menu
(`settings.c` + `menu.c`) draws one row per `SettingsMenuItem`, and SELECT calls
`settings_menu_push(row)` → the submodule's `init()` → a shared settings window
(`settings/window.c`, a `StatusBarLayer` + `MenuLayer` dispatching to
`SettingsCallbacks`) pushed onto the app window stack. BACK pops it.

### Nested window stack (the one launch-core change)

The foundation launch core pushed a system app's single window once from
`app_event_loop()`. Menu apps push a SECOND window (settings menu → submodule),
so the shared stack now tracks every `app_window_stack_push`:

- `app_window_stack_push()` (`watchface_sandboxed/src/port.c`), when privileged,
  runs the window's load+appear then calls `fw_window_stack_push()` — so nested
  pushes ride the same stack + click config + render.
- `app_event_loop()` (`system_app.c`) no longer pushes; it records
  `s_app_base_depth` (set by `fw_system_app_launch` before `main_func`) and pumps
  until BACK has popped every window the app pushed (depth back to base).
- `prv_window_pop()` (`launcher_ui.c`) now runs the popped window's
  disappear+unload handlers (mirrors shipping stack pop) so a submodule window
  frees its data / deinits its menu and BACK returns to the parent menu.

### Registration (the 3-edit hook, per app)

1. `app_registry.c`: `#include "apps/system/watchfaces.h"` +
   `"apps/system/settings/settings.h"`; rows
   `{ -6, "Watchfaces", watchfaces_get_app_info }` and
   `{ -7, "Settings", settings_get_app_info }`.
2. `CMakeLists.txt` `target_sources`: `src/apps_port_glue.c`, `watchfaces.c`,
   `settings/settings.c`, `settings/menu.c`, `settings/window.c`,
   `settings/health.c`, and `applib/ui/menu_layer_system_cells.c` (added to the
   graphics app-state group next to `menu_layer.c`), plus `lib/util/hash.c`
   (pulled by `text_layout.c`'s `graphics_text_layout_get_max_used_size`).
3. Per-file props: the real app sources + `apps_port_glue.c` get
   `-include fw_zephyr_pre.h` and `-iquote <watchface_sandboxed/include>` (so
   `process_state/app_state/app_state.h` resolves to the PORT app_state, which
   now also declares `app_state_get/set_user_data`). Resource IDs:
   `watchfaces.c` → `RESOURCE_ID_WATCHFACES_APP_GLANCE=0`,
   `RESOURCE_ID_MENU_LAYER_GENERIC_WATCHFACE_ICON=0`; `settings.c` →
   `RESOURCE_ID_SETTINGS_TINY=0`.

### Port backends (`src/apps_port_glue.c`) and ceilings

- **`AppMenuDataSource`** — port impl of the public API backed by the fw registry
  (lists entries whose md is `ProcessTypeWatchface`, synthesizing an
  `AppInstallEntry` so watchfaces.c's real filter runs). Keeps watchfaces.c 1:1.
  _ponytail: static list, no install/remove events, no per-app icons._
- **`shell/prefs`, `i18n`, `activity_prefs`, `watchface_get/set_default`** — RAM
  stores / identity functions. _ponytail: not persisted; no translation catalog;
  no real activity tracking._
- **`app_manager_put_launch_app_event`** — records the SELECTed watchface as the
  default + logs `WATCHFACE_SET <id>`. _ponytail: does not switch the running
  face; route through the shell once it's ported._
- **`StatusBarLayer`** — lean port drawing the title (real struct); the shipping
  `status_bar_layer.c` clock/window-stack machinery is skipped.
  _ponytail: title-only, no clock/animation._
- **Settings submodule stubs** — `settings_{bluetooth,notifications,vibe_patterns,
  quiet_time,timeline,activity_tracker,quick_launch,time,display,system}_get_info`
  each open a real `settings_window` with one "Not ported" row.
  _ponytail: replace each with its real submodule .c as its service deps land._
- Port header shadows extended (all under `zephyr-port-apps/**`, additive):
  `kernel/events.h` (+`PEBBLE_PREF_CHANGE_EVENT`), `kernel/pbl_malloc.h`
  (+`app_malloc`/`app_malloc_check`), `process_management/app_manager.h`
  (+`AppLaunchEventConfig`/`app_manager_put_launch_app_event`),
  `util/time/time.h` (+`DayInWeek`), `font_resource_keys.auto.h` (+GOTHIC keys).

### Real vs deferred

- **Watchfaces:** real (`watchfaces.c` verbatim). **Settings shell:** real
  (`settings.c`, `menu.c`, `window.c` verbatim). **Settings submodules:** Health
  real (`health.c`, no-HRM path); the other 10 are navigable stubs (see above).

### UART markers (what the orchestrator sees on hardware)

`LAUNCHER_SEL Watchfaces` / `LAUNCHER_SEL Settings` → `SYS_APP_LAUNCH <name>` →
`SYS_APP_LOOP depth=2`; entering a settings submodule → `WINDOW_PUSH … depth=3`;
BACK → `WINDOW_POP … depth=2`; BACK past root → `SYS_APP_EXIT <name>`. Selecting a
watchface additionally logs `WATCHFACE_SET <install_id>`.

### Known extra ceiling

- **Settings submodule window is pushed directly from the MenuLayer SELECT click
  callback** (`settings_menu_push`), i.e. the shared ClickManager is
  reconfigured while still inside the parent's click dispatch. Works for simple
  SELECT; if click-state corruption shows on hardware, defer the push to the loop
  top level the way the launcher defers `s_pending_md`.
