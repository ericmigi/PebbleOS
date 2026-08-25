# PebbleOS System-App Port Manifest (fan-out plan)

Survey of ~/dev/pebbleos system apps → port order for the Zephyr scaffold.

## Registration mechanism (how an app reaches the launcher)
- Master list: `src/fw/shell/normal/system_app_registry_list.json` (+ prf/sdk variants).
  Each entry: negative `id`, `enum`, `md_fn` returning `PebbleProcessMd*`; `ifdefs`/
  `target_platforms` gate build; `color_argb8` = launcher tile color.
- Codegen: JSON → `system_app_registry_list.auto.h` → `APP_RECORDS[]`
  (via `src/fw/apps/system_app_registry.h:4`).
- Consumer: `src/fw/process_management/app_install_manager.c` iterates `APP_RECORDS[]`,
  resolves MD via `app_install_get_md()`. Launcher menu built by
  `src/fw/process_management/app_menu_data_source.c`.
- MD: `src/fw/process_management/pebble_process_md.h` — `PebbleProcessMdSystem`.
  ProcessType: App=0, Watchface=1, Worker=2. Visibility: Shown=0, Hidden=1,
  ShownOnCommunication=2, QuickLaunch=3.
- To port an app: write `*_get_app_info()` returning a static `PebbleProcessMdSystem`,
  register enum/id/md_fn. (Port uses the foundation agent's registration hook — see
  SYSTEM-APPS-BUILDOUT.md.)
- Locations: apps in `src/fw/apps/system/`; watchfaces in `src/fw/apps/watch/`;
  shared UI in `src/fw/apps/core/`; `prf/` mfg-only + `demo/` (CONFIG_DEMO_APP_*) excluded.

## Ranked port order (easy → hard)
1. EASY (no service deps, validates MD/registry scaffold):
   battery_critical (`system/battery_critical.c:69`), toggles quiet_time/airplane_mode/
   motion_backlight/backlight_state (`system/toggle/*`), quick_launch_setup
   (`system/settings/quick_launch_setup_menu.c:86`).
2. tictoc default watchface (`watch/tictoc/tictoc.c:9`, ProcessTypeWatchface) → then bw/round faces.
3. Single-service apps: music (`system/music.c:1670`, now-playing svc), alarms
   (`system/alarms/alarms.c:476`, alarm+timeline), watchfaces picker (`system/watchfaces.c:199`).
4. One-heavy-service: weather (`system/weather/weather.c:747`, timeline), reminders
   (`system/reminders/reminder.c:291`), low_power face (`watch/low_power/face.c:152`),
   kickstart (`watch/kickstart/kickstart.c:647`, activity).
5. notifications (`system/notifications.c:836`) + timeline family (`system/timeline/timeline.c:1281/:1294/:1308`)
   — blob_db/pin_db + notification storage + timeline layout; do together.
6. settings (`system/settings/settings.c:200`, ~20 submodules, port submodule-by-submodule),
   health, workout, sports, send_text (broadest service closures), launcher last (shell surface).

## Watchface/gating notes
- Only 3 watchfaces: tictoc, kickstart, low_power. All else ProcessTypeApp.
- Hidden (system-launched, not in list): battery_critical, launcher, quick_launch_setup, health, low_power.
- QuickLaunch (button-assignable): 4 toggles, timeline/timeline_past, notifications_clear_history.
- ShownOnCommunication (need phone app): sports, weather, golf.
- Golf = resource app (stored binary, no fw code to port).

## Shared prerequisites to stand up in the scaffold
shell/prefs, resource/fonts loader, app_install_manager + app_menu_data_source, blob_db
(pin_db/contacts_db/watch_app_prefs_db), services/timeline, services/activity,
services/music, comm/BLE session layer.
