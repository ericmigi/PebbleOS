# QEMU parity plan — PebbleOS-on-Zephyr 1:1 vs FreeRTOS reference

Goal: real PebbleOS boots on Zephyr in qemu-pebble (pebble-emery machine) and
is pixel-identical + functionally identical to the shipping FreeRTOS build.
Hardware (obelix) comes later; nothing here may break the hardware path — all
QEMU-specific code stays behind the same driver seams the FreeRTOS build uses.

Reference: FreeRTOS `qemu_emery` build in qemu-pebble ("ref QEMU"). Same
source tree, same applib/services/apps/resources — pixel parity is achieved
by construction once the same code boots on Zephyr; the diff harness proves it.

## Phases

- **P0 Harness**: ref QEMU boots; scripted UI walk (sendkey via QMP socket)
  + screenshot capture; pixel-diff tool. Baseline PNG set committed.
- **P1 Zephyr QEMU drivers**: display (pebble-display @0x40008000,
  FB @0x50000000, 200x228 8bpp), buttons (pebble-gpio @0x40006000, IRQ 6),
  ext flash (pebble-extflash @0x40010000, XIP @0x10000000, SYNC handshake),
  RTC (@0x40005000). Zephyr driver-model drivers in zephyr-port/module.
  Gate: gfx demo renders colorbars+text in QEMU, buttons produce events,
  flash read/write/erase round-trips.
- **P2 Demo apps on qemu_emery**: kernel spine, watchface (Sliding Text)
  ported from pt2 overlays to qemu_emery. Gate: watchface renders in QEMU,
  screenshot matches ref-rendered equivalent.
- **P3 Consolidation (plan P2.2)**: one Zephyr firmware booting the real
  PebbleOS path — main.c init order, 9 tasks as Zephyr threads via pbl/os
  seam, event loop, services, PFS on flash API, resources from SPI flash
  image, launcher. Incremental: boot→console, +flash/PFS, +resources,
  +display/compositor, +launcher UI, +buttons, +apps.
- **P4 Parity loop**: scripted UI walk on both images, screenshot diff every
  screen; fix until zero diff. Functional checks: console commands, app
  launch/exit/force-close, notifications, timers.

## Rules

- **Pixel bar (highest level, user-set): frame-exact 1:1.** Every screen AND
  every animation frame must be pixel-identical to the FreeRTOS reference
  (px_diff == 0). Compare under deterministic state: same `-rtc base=` fixed
  clock, same flash image, same scripted input. Capture frame bursts during
  transitions (launcher slide, Sliding Text slide-in/out), not just settled
  screens. A nonzero diff is a bug to fix, never to accept. Known offenders:
  Sliding Text animation collapsed by the synchronous animation stub; custom
  launcher menu != real launcher app.

- FreeRTOS build must stay green (`./pbl configure --board qemu_emery &&
  ./pbl build`) — it is the reference and the shipping firmware.
- Commit per milestone. gitlint clean.
- QEMU-only code lives in drivers/board layers only; kernel/services changes
  must be RTOS-seam-clean (pbl/os), never #ifdef ZEPHYR in shared code
  unless the seam demands it.
- Zephyr workspace: ~/dev/pblboot-ws (west, ericmigi/zephyr@pt2-display).
  Build: see RUNBOOK.md §6.

## Animation capture

Two tiers:
- ui_walk `burst:` samples screendumps (~10-15/s) and dedupes — quick, catches
  gross diffs, can miss individual frames.
- EXACT: qemu frame recorder (local qemu fork checkout
  ~/dev/qemu-pebble-src, branch pebble-display-recorder, binary
  build/qemu-system-arm). `-global pebble-display.record-dir=DIR` dumps the
  raw framebuffer as PPM on every guest CTRL_UPDATE plus frames.log with
  virtual-ns timestamps. Run both firmwares with the recorder and diff the
  frame streams 1:1. Not pushed anywhere (never push coredevices).

## Status log

- P0 DONE 2026-08-31: ref runs headless (SDL dummy), ui_walk.py + px_diff.py in zephyr-port-notes/tools, PT2 UX mapped (boot=watchface, select=launcher, up/down=timeline).
- P1 DONE 2026-08-31: display+buttons+rtc+extflash Zephyr drivers verified in qemu-pebble (gfx renders via applib, 4 buttons report, RTC sane, flash persists via SYNC). gfx app builds for qemu_emery.
- P2 DONE 2026-08-31: Sliding Text watchface (real PBW, real loader) renders correct wall-clock time on qemu_emery. Known issue: kernel wall_clock_zephyr.c applies the SF32 month -1 normalization to the Zephyr-compliant QEMU RTC too — date off by one month on qemu; make it rtc-driver-conditional (HH:MM unaffected).
- TODO parity fixture: reconstruct Sliding Text .pbw from the embedded
  loader bin + resources (zip: manifest + pebble-app.bin + resource pack),
  install into the ref emulator via pebble-tool (port 12344), then
  frame-diff Sliding Text zephyr vs ref incl. slide animations. Blocked
  offender: port's synchronous animation stub collapses slides — needs the
  real animation service in the fw app.
- Sliding Text px audit 2026-08-31 (same minute, embedded bin == store
  v1.2.1 emery bin, byte-identical): 6266 px differ. Two causes:
  (1) zephyr text frozen 88px right of ref, right-edge clipped — the
  synchronous animation stub leaves layers at mid-slide positions; fix =
  real animation service, frame-exact burst diff after.
  (2) capture hygiene: ref must be captured backlight-lit (send 'back',
  shoot within ~3s); zephyr is always full-bright.
- Fixture: Sliding Text v1.2.1 via
  https://appstore-api.repebble.com/api/v1/apps/uuid/c7a9d535-e9bd-4c36-9c30-b45ad0908634
  (pbw_file in latest_release); install: pebble install --qemu localhost:12344 <pbw>.
- P4 progress 2026-08-31: real shell flow on qemu_emery — boots real
  TicToc, real launcher app, real Settings; settled screens launcher /
  launcher+down / settings at px_diff == 0 vs reference (verified twice,
  cold boot). Remaining P4 queue, in order: real animation service
  (running), qemu frame recorder for exact animation diffs (running),
  backlight keypress dynamics, watchface pref persistence, timeline,
  glance backends (music/alarms/weather), notifications row backend.
  Note: qemu fw build consumes resource_ids.auto.h +
  timeline_resource_table.auto.c from the FreeRTOS reference build dir —
  build the reference first (acceptable: ref build is required for
  parity anyway).
