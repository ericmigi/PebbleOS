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

- FreeRTOS build must stay green (`./pbl configure --board qemu_emery &&
  ./pbl build`) — it is the reference and the shipping firmware.
- Commit per milestone. gitlint clean.
- QEMU-only code lives in drivers/board layers only; kernel/services changes
  must be RTOS-seam-clean (pbl/os), never #ifdef ZEPHYR in shared code
  unless the seam demands it.
- Zephyr workspace: ~/dev/pblboot-ws (west, ericmigi/zephyr@pt2-display).
  Build: see RUNBOOK.md §6.

## Status log

- P0 DONE 2026-08-31: ref runs headless (SDL dummy), ui_walk.py + px_diff.py in zephyr-port-notes/tools, PT2 UX mapped (boot=watchface, select=launcher, up/down=timeline).
- P1: started 2026-08-31 (3 agents: display+gfx, buttons+rtc, extflash).
