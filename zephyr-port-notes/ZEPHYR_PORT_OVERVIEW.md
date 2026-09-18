# PebbleOS on Zephyr — Port Overview

Canonical status of the FreeRTOS→Zephyr port of PebbleOS, targeting the
Core Devices **obelix** (SF32LB52, "Pebble Time 2") and a **qemu_emery**
emulator board. Driven from the commit history on branch `zephyr/obelix`
(874 commits ahead of `main`; port work runs 2026-06-11 → present).

The milestone logs (`STATUS.md`, `PROGRESS.md`, `ROADMAP.md`,
`NORTH_STAR.md`) are the blow-by-blow record. This file is the high-level
map: what exists, what is tested, what remains — read this first.

## What the port is

The firmware runs on **Zephyr** (kernel, drivers, MPU/SVC, NimBLE host)
while keeping the **real PebbleOS application and service code** — the
same `src/fw` graphics stack, applib, timeline/blob_db/notification
services, settings, and system apps that ship on FreeRTOS. The seam is a
`pbl/os` backend plus glue in `zephyr-port-apps/fw/`; shared code compiles
unchanged wherever possible. The goal is 1:1 functional **and** frame-exact
pixel parity with the FreeRTOS build, verified in qemu_emery and on obelix.

Two boards, one app:
- **pt2** = obelix SF32LB52 hardware. NimBLE host + external SF32 LCPU BLE
  controller, shipping SiFli QSPI flash driver, pblboot dual-slot + PRF.
- **qemu_emery** = the `pebble-emery` QEMU machine (coredevices/qemu fork).
  Deterministic; drives the automated parity/regression suite.

## Architecture spine

- **Kernel seam** (`pbl/os` Zephyr backend): tasks, timers, semaphores,
  mutexes, RTC-backed `tick_timer_service`. FreeRTOS API shimmed onto
  Zephyr primitives (`252fdb7f8`, `4b3cf8026`, `0998c2e6a`, seam audit
  `af704b106`).
- **Flash / PFS**: the shipping SiFli QSPI driver mounts PFS on external
  NOR; self-heals a clobbered region (`64514fdea`, `810a23970`,
  `425105ba9`).
- **Graphics**: real applib graphics renders into a RAM framebuffer,
  pushed to the JDI panel driver; real animation engine + compositor
  shutter/moook transitions linked in (`bb37c1f6c`, `de3fa759c`,
  `0da933ec0`, `0dc34c1bc`).
- **Sandbox** (`zephyr-port-apps/watchface_sandboxed/`): third-party PBWs
  run unprivileged behind a custom ARMv8-M MPU/SVC hook on Zephyr threads
  (`CONFIG_USERSPACE=n`), syscalls via svc#4 (`0cdd96d8a`, `86e1c59b8`,
  `487a77405`).
- **Shell**: real launcher app, system-app launch path, window stack +
  KernelMain UI pump (`8f9034188`, `0c0f86a3c`, `449941708`).
- **BLE**: NimBLE host + SF32 LCPU controller + PPoGATT; bond reused from
  PRF, GATT re-discovery on reconnect (`84dd76aa6`, `407450e90`,
  `ad8390f3c`). RF cal fix `d88c52881`.
- **OTA**: packages as a bootable slot-0 `.pbz`; receives a fw pbz over
  BLE and A/B-installs to slot1 under pblboot (`fd3cc8c5f`, `722e54bf1`).

## Built and tested

### Boot + shell
Unified firmware boots on both boards; PFS mounts; app registry
enumerates; launcher renders with icons + status bar; idle-timeout returns
to the watchface. Pixel-exact settled screens vs FreeRTOS
(`563c40bd3`, `154804da1`, `873e563e0`, `c032b8267`).

### Watchfaces
TicToc + Digital built-in; real Sliding Text PBW runs sandboxed on
hardware (the original North Star, `03d5200fe`/`ed671c3cd`). Picker
switches the running face; selection persists across reboot
(`8736ec4b5`, `b3aa6a1bb`, `8af0d5b57`).

### Settings (full sweep, pixel parity)
Bluetooth, Display, System, Notifications, Quiet Time, Sounds & Haptics,
Timeline, Quick Launch, Background App, Date & Time — real submodules, not
stubs; toggles persist via the real shell/notification prefs
(`d06e4f762`, `63ce8d08a`, `9b52feecc`, `ce11d7f8b`, `c76718069`,
`b0799b5b4`, `acb1a1509`). System→Information shows real fw version, BT
address, OTP serial/HW version on pt2 (`ddc4f558e`); hang fixed
(`15e1595d7`).

### Notifications
Full popup chrome: real timeline notification layout, status-bar clock,
swap_layer multi-notification nav, peek intro animation, action menu
(SELECT→Dismiss, Clear All), auto-dismiss on window timeout refreshed on
interaction, Quiet Time suppression. Routed through the **real**
notification service + storage; history app with per-item detail + dismiss
(`27be73fe7`, `71a57ef17`, `f99896e1b`, `4dec258c8`, `3741b6952`,
`ebb85644a`, `f8882185f`, `b658e58a1`, `dd4f1b0a1`, `8539c6934`,
`9eebcbbad`).

### Timeline
Real timeline item store + `pin_db` (blob_db bricks); Timeline Future/Past
apps list pins and render them as **real layout cards**; swipe between
cards (`50b9e774f`, `1c4a4e51f`, `f1643e480`, `3c6cdd1d7`, `5c0a97458`).

### Alarms
Real alarm service: create / persist / fire / popup / snooze / dismiss +
timeline pin via pin_db (`ac78de971`, `19ec98919`, `d73967d8b`).

### Music / Weather / Battery
Music now-playing, play/pause, track progress bar (live via events);
weather forecast in the launcher glance with type→icon mapping; battery
state. Per-second tick drives clock + progress (`7681bec51`, `32a26670d`,
`a174b4593`, `8b75733db`, `e08a77b8a`, `08da60142`, `f88ee07f1`).

### BLE + OTA (hardware-verified)
Zephyr boots the SF32 controller; NimBLE host + PPoGATT; pairs by reusing
the PRF bond so pairing survives PRF→Zephyr. Slot-0 `.pbz` OTA install and
Zephyr self-OTA (A/B to slot1) both verified on hardware
(`140a8bbca`, `c65cc9913`).

### Real shell on hardware (obelix)
Since 2026-09-08 the full real-shell fw runs on obelix, not just qemu:
pairs with CoreApp, receives BlobDB notifications, renders the card.
Bonds persist through shared-PRF storage; console-UART button injection
for headless nav; app-exit animation/timer teardown
(`b2aae2319`, `64a2c0efd`, `9b0ca9789`, `823a33d8c`).

### PBW install from phone (2026-09-11)
Full phone→watch app pipeline: CoreApp locker syncs into the real
`app_db` (BlobDB db 2); launcher lists installed apps; launching one not on
PFS sends AppFetch (0x1771); phone streams app/resources/worker over
PutBytes into `@<id>/app|res|worker`; loaded from PFS and run on
KernelMain with a header-synthesised md; app resource packs + custom fonts
served from PFS. `APP_RUN_STATE` run-by-uuid; provisional AppDB entry for
un-synced uuids. Missing SDK symbols get weak `APPLIB_MISSING` stubs; the
by-value window ABI, pbl_std time/locale, persist (real service),
connection/battery subscriptions provided
(`d6cf221c7`, `0bab6f79d`, `f390f3b49`, `0f03e9400`).
Verified on qemu: TimeStyle and Bearing install, fetch, load, run, render.
Verified on obelix through launch (TimeStyle).

### Crash/stability fixes (2026-09-11)
- Watchface-PBW relaunch churn (face relaunched ~3×/sec, starved BLE) and
  the launcher glance-icon panic (stale resource bank NULLed a SYSTEM
  icon → assert boot-loop). Both fixed; resource bank scoped to the
  running app via the launch bracket (`bdb019bf5`).
- App-exit teardown of animations + app timers (`823a33d8c`).
- Peripheral-initiated pairing fallback for the `0x213` reconnect stall
  (`2b38ec189`).

## QEMU regression suite

`zephyr-port-notes/tools/qemu_test_suite.py` — 24 end-to-end tests, HTML
report stamped with the fw build hash (`ff2519219`). Currently **24/24**
on `2b38ec189`. Tests: boot_launcher, watchface_tictoc, clock_ticks,
notif_popup, notif_stays, notif_action_menu, notif_dismiss_removes,
notif_clear_all, notif_history_app, music_now_playing, music_progress_ticks,
music_pause_freezes, weather_glance, battery_glance, settings_app,
alarms_app, timeline_pins_list, timeline_card, timeline_swipe,
quiet_time_suppresses, watchface_switch, watchface_persists_reboot,
settings_system_information, no_fatal_overall.

`quiet_time_suppresses` is occasionally flaky (drives DND via a key
sequence); passes on rerun.

## What remains

**Open bugs:**
- **fctx antialiased render on the 8-bit color panel.** TimeStyle/Bearing
  clock digits render on qemu (1-bit framebuffer, fctx `_bw` path) but are
  blank on obelix (8-bit color, fctx `_aa` path: full-screen flag buffer +
  `gbitmap_get_data_row_info`). The one confirmed hardware-only render bug.
- **BLE re-pair not confirmed end-to-end on hardware.** Both bonds were
  cleared and the peripheral-initiated fallback added, but a full fresh
  pair was never observed landing. The reconnect loop was a bond desync
  (`Key missing` / GATT status 5), not a firmware crash.

**Not built / stubbed:**
- **Health** service and app.
- **Worker** apps (background workers) — PutBytes object type wired for
  transfer, not run.
- **AppMessage** — PBW config/messaging endpoints are `APPLIB_MISSING`
  weak stubs (many watchfaces call `app_message_open` etc.).
- **Time sync from phone** — watch clock does not track CoreApp time on
  this build (noted during hardware testing).

**Test-harness limits (not firmware faults):**
- The watchface picker's selection-layer swallows injected serial clicks,
  so TimeStyle-as-face could not be selected headlessly on hardware.

## Build + flash quick reference

- qemu fw: `bash ~/dev/pblboot-ws/fwbuild.sh` → `build-fw-qemu/zephyr/zephyr.elf`
- pt2 fw: `bash ~/dev/pblboot-ws/fwbuild-pt2.sh` → `build-fw-pt2/zephyr/zephyr.bin`
- header + flash slot0: `zephyr-port-apps/fw/tools/prepend_firmware_header.py`
  then `sftool write_flash …@0x12020000` (see `RUNBOOK.md`)
- package pbz: `zephyr-port-apps/fw/tools/package_pbz.py`
- OTA install / self-OTA: `OTA-PBZ-RUNBOOK.md`
- qemu suite: `python3 zephyr-port-notes/tools/qemu_test_suite.py`
- emulator workflow: `docs/development/qemu.md`
