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
