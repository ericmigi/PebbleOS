# North Star: Sliding Text watchface ticking on real obelix (Zephyr)

Target: the published third-party watchface **Sliding Text** (apps.repebble.com
/sliding-text_555a3ce1c2afa28d5800000f, PBW 1.2.1) rendering + ticking on the obelix
watch running Zephyr — no modification, the real emery-platform binary.

Why this watchface: pure C, only `tick_timer_service` + `text_layer`/`layer`/`window` +
fonts. No AppMessage, no Bluetooth, no buttons, no persist, no sensors. Exercises the full
app path minus comms — the cleanest possible end-to-end proof that "PebbleOS runs apps on
Zephyr."

Target binary: `sliding-text-pbw/emery/pebble-app.bin` + `app_resources.pbpack`
(obelix = emery platform).

## The four required pieces (parallel swarm streams)

Each stream is an isolated Zephyr app in its own directory, UART-verifiable without the
others, so they progress independently and integrate at the end.

- **S1 — Kernel spine** (`zephyr-port-apps/kernel/`): PebbleOS event_loop + task_timer/
  new_timer + tick_timer_service as Zephyr threads. Gate: `KERNEL_UP` then `TICK <time>`
  once per second on UART. Owns src/fw/kernel + timer services.
- **S2 — Display driver** (zephyr fork, `drivers/display/sf32lb_jdi.c`): the long pole —
  first Zephyr driver for obelix's JDI/LCDC panel, ported from src/fw/drivers/display/
  sf32lb/display_jdi.c. Gate: `DISPLAY_EOF` (LCDC transfer completes without wedging) on
  UART; pixels need a human eye (rig has no panel camera).
- **S3 — Graphics stack** (`zephyr-port-apps/gfx/`): real applib graphics rendering
  "12:34" into a RAM framebuffer. Gate: stable `GFX_CRC` + an ASCII preview of the text
  over UART — no display needed. Owns src/fw/applib/graphics.
- **S4 — Loader + syscalls** (`zephyr-port-apps/loader/`): PBW process loader (parse
  header, relocate, patch jump table) for the real sliding-text emery binary, plus wiring
  the proven svc#4 sandbox hooks into the real DEFINE_SYSCALL path. Gate: `LOADER_OK`
  (real binary parsed + relocated + CRC valid) on hardware. Owns src/fw/process_management
  + src/fw/syscall.

## Integration (after streams land)

S3's framebuffer → S2's display_write (converge graphics + panel). S4's loaded app runs on
S1's app task, calls graphics via the jump table (S3) and gets ticks from S1, syscalls via
S4's wired hooks. Final: load the real PBW, run it unprivileged in the sandbox, it
subscribes to ticks and draws the clock, S2 pushes the framebuffer to the panel. That is
the North Star.

## Milestone mapping (from STATUS.md ladder)

This North Star = M-Kernel (S1) + app-load/M-Sandbox-real (S4) + M-Display (S2) +
graphics (S3). It deliberately SKIPS M-BLE, M-System (OTA/recovery), buttons/touch, power.
It is a strict subset of "wearable," chosen as the first undeniable "it's a Pebble" demo.

## Status
Swarm launched. All four streams building their isolated Zephyr apps against the pt2 board.
Verification per stream is UART on the unicorn-mac-1 rig (flash via known-good step.py).
