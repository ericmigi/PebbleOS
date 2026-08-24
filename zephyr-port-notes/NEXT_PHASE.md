# Next phase — P2: one real PebbleOS image on Zephyr

## Context in the whole project

Project = PebbleOS off FreeRTOS/waf onto Zephyr (plan v2, phases P0–P6,
obelix-first). This session built a **vertical slice** — the thinnest end-to-end
path proving every hard assumption on real silicon. That is done and green on
hardware:

- P0 (de-risk): the Pebble MPU/SVC sandbox rides on Zephyr (svc#4, fault
  containment, 57-line arch patch — under the kill criterion).
- P1 (groundwork): pbl/os seam; FreeRTOS obelix build stays green; kernel/tick,
  graphics, PBW loader, JDI display driver all verified on hardware.
- North Star demo: the real published Sliding Text PBW executes on
  PebbleOS-on-Zephyr and renders correctly (real Gotham, upright, PT time) on the
  physical obelix panel.

The demo is **scaffolding**, not the product: N standalone Zephyr sample apps in
`zephyr-port-apps/`, each proving one mechanism in isolation, with stubs,
privileged execution, and no BLE/PFS/OTA/input. The pieces work; they are not yet
one firmware. What carries forward: the commits, the plan docs, and the
hardware-verified findings (the 12-region MPU map, the FreeRTOS-vs-Zephyr
semantic deltas, the exact syscall/jump-table wiring). The sample apps mostly get
absorbed and deleted.

## P2 — the next milestone: real PebbleOS boots on Zephyr

Turn the proven pieces into the actual firmware, not a gallery of demos.

- **P2.1 (do first — closes P0): merge + verify the sandbox for real.** Merge the
  sandbox-spike arch hooks from the fork's `pt2-display` branch into `pblboot`
  (so every build has `CONFIG_ARM_CUSTOM_SVC_HOOK` / `..._THREAD_RESTORE_HOOK`).
  Flash `watchface_sandboxed` (built, not yet hardware-run) and verify the real
  watchface runs UNPRIVILEGED through svc#4: `SANDBOX_APP_UP`, `SANDBOX_SYSCALL_OK`,
  `SANDBOX_TICK`, plus a `SANDBOX_FAULT_CONTAINED` from a deliberate OOB access.
  Highest value, lowest risk; the security gate P0 promised.
- **P2.2 Consolidate to one image.** Fold kernel + pbl/os + loader + display +
  graphics into a single Zephyr firmware that boots the real PebbleOS path
  (main.c -> the 9 tasks as Zephyr threads -> event_loop -> services), not
  separate sample apps.
- **P2.3 PFS on the QSPI NOR.** The flash filesystem, so apps/resources/settings
  persist and load from storage instead of embedded C arrays. (Flash read already
  proven; write/erase + PFS mount next.)
- **P2.4 App registry + launcher.** Real system launcher; apps loaded from PFS;
  install a stored PBW and run it sandboxed.

Gate: real PebbleOS boots to a launcher on obelix under Zephyr; a stored PBW
installs from the filesystem and runs sandboxed.

## P3+ toward a wearable

- BLE: port NimBLE's one OS-coupling file (`npl_os_pebble.c` -> Zephyr), bring up
  the SF32 controller, pair with phone, app install over BLE.
- Input: buttons + touch through Zephyr.
- OTA / recovery / coredump: dual-slot, rollback, PRF, coredump round-trip.
- Resources: load fonts/images from the real system_resources.pbpack instead of
  embedded packs.
- P4 parity + narrow cutover (main ships Zephyr for obelix, FreeRTOS kept one
  emergency cycle); P5 getafix + asterix; P6 delete FreeRTOS + waf.

## Cosmetic / cleanup debt from the demo (small)

- Fonts: the watchface embeds the app's own emery pbpack; real firmware should
  serve fonts from system_resources.pbpack.
- Orientation is correct now (native scan; software fb rotation removed). The JDI
  driver's `HAL_LCDC_LayerVMirror` is available if a driver-level flip is ever
  wanted.
- Remove KERNEL_DEMO_EPOCH before shipping; drive time from the real RTC/timezone.
- The watchface runs privileged in the base demo; the sandboxed variant (P2.1) is
  the real path.

## Recommended immediate next step

**P2.1** — merge the arch hooks and verify `watchface_sandboxed` on hardware. It
closes the P0 security gate for real and is the foundation P2.2–P2.4 build on.
