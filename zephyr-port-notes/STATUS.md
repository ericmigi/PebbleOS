# PebbleOS → Zephyr port: status

_Snapshot: 2026-08-24, obelix-first track. Companion to `zephyr-port-plan.md` (the
adversarially-reviewed v2 plan) and `PROGRESS.md` (the raw run log)._

## The broader plan

Port PebbleOS off FreeRTOS + waf + arm-none-eabi-gcc onto **Zephyr** (Zephyr kernel,
CMake/west/devicetree build, mainline Zephyr drivers where they're good), while holding
two things inviolable: **PBW binary compatibility** (existing third-party apps run
unmodified) and **weekly shippable firmware** (no long feature freeze). The strategy was
stress-tested in two rounds against a skeptical reviewer; the settled shape is:

- **Sandbox:** keep Pebble's own MPU/SVC app sandbox riding on Zephyr threads
  (`CONFIG_USERSPACE=n`) via a tiny, bounded Zephyr arch patch — *not* a rewrite onto
  Zephyr-native userspace. Hard kill criterion: if the arch patch exceeds ~75 lines or
  spreads beyond two files, stop and reconsider.
- **Transition:** dual-build integration train, not a hard-cutover branch and not a
  Kconfig-selectable backend. Main keeps shipping FreeRTOS; the Zephyr target lands in
  the same tree as a non-shipping second build, agents rebase daily, and only a 2–4 week
  narrow freeze happens at the very end.
- **Sequencing (user override):** **obelix (SF32LB52) first**, not asterix. obelix has
  the hardware lab (unicorn-mac-1: Pixel 7 + watch on a PPK2) and a known-good flash
  recipe, so it's the fastest path to hardware-verified progress.

Phase ladder (from the plan): **P0** architecture/contract spike → **P1** mainline-neutral
groundwork → **P2** Zephyr kernel boots real PebbleOS in QEMU → **P3** obelix hardware
vertical slice (boot/OTA/flash/BLE/drivers) → **P4** parity + cutover → **P5** remaining
boards (getafix, then asterix) → **P6** retire FreeRTOS + waf.

Schedule target (debate-settled, AI-swarm execution + one hardware lab): first board
shipping on Zephyr in **5–7 months**, all boards **7–10**, FreeRTOS+waf deleted **8–11**.

---

## Where we are: P0 + P1, running in parallel

We are in the **P0 spike** and **P1 groundwork** phases simultaneously — the two are
separable and both are underway on obelix hardware. This is the earliest, highest-leverage
part of the whole project: prove the load-bearing assumptions on real silicon before
committing to the long build-out.

### P0 — architecture/contract spike

**The single biggest risk in the entire port is retired.** On the real obelix watch:

- ✅ **Sandbox mechanism proven green on hardware.** Pebble's foreign MPU/SVC sandbox
  runs on Zephyr threads: an unprivileged app thread issues `svc #4`, a custom hook
  elevates it, it reads kernel state and returns to unprivileged (`PASS_SYSCALL`); a
  deliberate out-of-bounds read faults, the overridden fatal handler catches it and kills
  only that thread (`PASS_FAULT_CONTAINED`), and the kernel keeps running
  (`PASS_KERNEL_ALIVE`). **57 lines** added to Zephyr arch files — **under** the 75-line
  kill criterion. `CONFIG_USERSPACE=n`.
- ✅ **Real hardware MPU map captured** (a deliverable in itself): 12 regions, only 2 used
  by the SoC/Zephyr (XIP flash is already unprivileged-executable, SRAM is privileged),
  **10 regions free** for Pebble's per-task sandbox — comfortably enough.

**P0 still open** (contract work, not yet started): the frozen PBW ABI conformance spec,
exact ROM/RAM budgets per board, and the force-close / process-lifecycle contract written
as tests. These gate P3, not P2.

### P1 — mainline-neutral groundwork (keeps FreeRTOS 100% shipping)

- ✅ **Zephyr boots on obelix** — custom `pt2` board on the coredevices Zephyr fork
  (4.3.99), flashed at the bootloader slot via the known-good rig recipe. hello_world +
  interactive shell (scheduler, threads, device tree) verified over UART.
- ✅ **pbl/os Zephyr backend, 7/7 on hardware** — the OS seam (`include/pbl/os`)
  reimplemented on Zephyr `k_*` primitives; a Zephyr app compiles **real PebbleOS code**
  (circular_buffer, crc32, reliable_transport) against it and self-tests green on the
  watch. Six FreeRTOS-vs-Zephyr semantic differences documented (always-recursive
  mutexes, +1-tick timeouts, 10 kHz vs 1 kHz tick, non-owner unlock, etc.).
- ✅ **External QSPI NOR reads under Zephyr** — the flash the filesystem lives on;
  read-verified on hardware. DMA/I2C/GPIO/PMIC all enumerate.
- ✅ **nPM1300 PMIC battery/charger readback on hardware** — mainline Zephyr driver;
  VBATT 3808 mV (matches the PPK2 supply), die temp 25 °C, charger state — all correct.
- ✅ **FreeRTOS seam audit** — the map for the whole Z0 routing effort: 109 sem/mutex +
  51 queue + 9 delay + 4 task-create sites are mechanical; 59 critical-section + 52
  TCB/scheduler + 15 ISR need design; no event-group/timer/notification usage at all.
  First file (`reliable_transport.c`) routed through the seam as proof.
- ✅ **Idle-power baseline** logged: 3.38 mA @ 3.8 V, no power management (a reference,
  not a parity claim).

**P1 in flight right now (three live streams):**
- 🔄 **FreeRTOS build regression check** — verifying the shipping obelix firmware still
  builds with the seam routing applied. Worktree is being made a self-contained build
  environment (real venv installed; all submodules initializing). This is the gate that
  says "we haven't broken the product." _Closest thing to done; blocked only on submodule
  clone + one build._
- 🔄 **Next seam-routing batch** (Sol) — routing the next mechanical FreeRTOS family
  (queues / remaining mutexes) through the seam, both backends in lockstep.
- 🔄 **Zephyr PM / tickless idle** (Sol) — bringing up SF32LB52 low-power idle to drive
  the 3.38 mA baseline down; PPK2-measurable.

**P1 not yet started:** the clar unit-test harness port from waf to CMake/CTest (large,
scheduled to land after the compiler/kernel work, not a blocker for P2).

### How close is the current milestone?

The **risk-retirement goal of P0+P1 is essentially met**: every load-bearing assumption —
sandbox fits in the MPU budget, Pebble code runs on the Zephyr OS seam, the flash and PMIC
work, FreeRTOS can be routed through a seam without a rewrite — is now demonstrated on the
actual obelix hardware, not on paper or in simulation. What remains in P1 is **breadth and
confirmation**, not open questions: finish routing the mechanical seam families, confirm
the FreeRTOS build is untouched, and (later) port the test harness. Call it **the hard part
of P0+P1 done; the mechanical tail in progress.**

---

## Next milestone: P2 — Zephyr kernel boots *real* PebbleOS

The next real milestone is booting a meaningful slice of **actual PebbleOS firmware** (not
a smoke app) on the Zephyr kernel with the Pebble sandbox active — first in QEMU, then on
the obelix watch. Concretely:

1. Stand up the 9 Pebble tasks as Zephyr threads on the `pbl/os` backend (kernel-mode for
   now), with `event_loop` and `new_timer` running.
2. Bring PFS (the flash filesystem) up on the Zephyr flash API over the QSPI NOR we've
   already proven readable.
3. Wire the sandbox spike's arch hooks into the real `DEFINE_SYSCALL` path so a stored PBW
   can run unprivileged and syscall into the firmware.
4. Gate: the launcher runs and an existing PBW installs and executes — first under QEMU,
   then flashed to obelix — with force-close and fault semantics matching the frozen
   contract from P0.

Reaching P2 turns "the pieces work individually on hardware" into "PebbleOS runs on
Zephyr." After that, P3 is the obelix hardware vertical slice (OTA, recovery, BLE via the
NimBLE npl port, displays, power parity) — the push to a watch you could actually wear.
