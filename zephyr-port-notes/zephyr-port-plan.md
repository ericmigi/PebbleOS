# PebbleOS → Zephyr Port Plan (v2 — post-debate)

Status: v1 debated adversarially with Codex (gpt-5.6-sol), two rounds, both
sides reading the tree. v2 merges the outcome. Strategic frame unchanged:
Zephyr-first; build system falls out of the port; Python-in-tooling accepted;
browser-firmware out of scope; in-browser app SDK remains its own working track.

## 0. Goals / non-goals

Goals:
- PebbleOS as a Zephyr application: Zephyr kernel + CMake/west/devicetree
  build; waf and the FreeRTOS fork fully retired.
- PBW binary compatibility inviolable (frozen ABI, spec'd in P0).
- Boards: asterix (nRF52840) first, then getafix + obelix (SF32LB52), plus
  the 3 QEMU boards.
- Power parity within explicit per-scenario budgets on the PPK2 rig.

Non-goals: firmware-in-browser; Python removal; clang as firmware compiler
(later option; the LLVM audit's Phase-0 source hygiene lands anyway);
PFS/applib/services rewrites; Zephyr-native userspace (deferred, see §3);
Zephyr BT host (NimBLE stays).

## 1. Assets

- `lib/os` + `include/pbl/os` seam wrapping FreeRTOS.
- Gerard's Zephyr convergence merged: kconfiglib Kconfig (196 files),
  boards/*.yml + defconfig, west-style runners, picolibc, subsys/,
  include/pbl reorg. prj*.conf already Zephyr-convention.
- pblboot already a Zephyr app (west.yml, CMake, board defs).
- SoCs mainline: nRF52840 first-class; SF32LB52 in Zephyr 4.3 (dts, board,
  hal_sifli incl. BT) — upstreamed by Core Devices.
- Memfault Zephyr port exists (adopt deliberately or keep Pebble writer —
  decision in P3, not both).
- Test infra: 287 clar host test binaries; QEMU generic pebble machines;
  unicorn hardware lab (per-PR QA, PPK2); remote-input endpoint 0xf00d for
  scripted UI-walk screenshot diffs.

## 2. Coupling inventory (verified)

- 81 files use FreeRTOS beyond the seam; 9 static tasks; 228 DEFINE_SYSCALL
  sites emitting `svc 2` from firmware-side wrappers (PBWs call jump-table
  exports — they never embed SVC; renumbering is not an ABI change).
- FreeRTOS fork surgery to re-achieve: CM33 dedicated privileged syscall
  stack + PSPLIM; coredump original-SP/FPU-frame recovery; alternative timers.
- App region is executable+writable RAM mixing code/heap/stack — deliberate
  W^X exception, must be named in the threat model; incompatible with Zephyr
  userspace assumptions (a driver of the §3 decision).
- MPU reality: nRF52840 = 8 regions (tight: flash map, SRAM map, user stack,
  app/worker RWX, shared RO, guards all compete). SF32 = 16 regions; SiFli
  vendor code owns 0–4, Pebble installs task regions at 8–11. ARMv7-M
  subregion-disable tricks in mpu_armv7m.c have no ARMv8-M equivalent.
- PM is mechanism+policy fused per SoC: soc/nrf/nrf52/freertos.c (sleep depth,
  flash power-down, RTC wakeup, synthetic tick, icache) and
  soc/sf32lb/sf32lb52x/freertos.c (LCPU IPC idleness, NVIC save/restore, QSPI
  deep-power-down, DLL clocks/rails, LPTIM, GTIMER time compensation,
  3-level sleep blocking). Ported as first-class SoC projects.
- NimBLE npl: header embeds FreeRTOS types; HFXO acquisition tied to sleep
  blocking; init.c creates FreeRTOS tasks/semaphores; SF32 controller-reset/
  coredump hooks. Port = 2–3 wk + soak.
- process_manager force-close contract: waits for privileged syscall exit,
  injects DEINIT to unblock, suspends only when safe. Must be preserved
  verbatim; "kill thread" is not equivalent.
- Zephyr 4.3/4.4 SF32 QSPI userspace-reachable CVE (fix → 4.5): backport to
  pinned tree; does not force uprev.

## 3. Sandbox decision (settled in debate)

**Keep Pebble's MPU/SVC sandbox on Zephyr threads. CONFIG_USERSPACE=n.**
Zephyr-native userspace rejected for the port because PBWs are not Zephyr
apps: frozen ABI, no Zephyr object handles, executable+writable app RAM, and
228 syscall verifiers whose *security semantics* (pointer validation, TOCTOU,
ownership, blocking-during-force-close) would each need re-certification —
4–8 engineer-months of audit plus recurring cost per new syscall. Deferred as
a possible later security migration.

Mechanism (bounded Zephyr patch, not a fork):
- `arch/arm/core/cortex_m/svc.S`: dispatch one reserved foreign SVC (Pebble
  moves firmware wrappers svc 2 → svc 4; Zephyr owns 2/3) before Zephyr's
  handling. ~20–35 lines.
- `arch/arm/core/cortex_m/swap_helper.S`: late restore hook for incoming
  thread — programs CONTROL.nPRIV, Pebble's reserved dynamic MPU slots,
  PSPLIM state. ~10–20 lines. Use Zephyr's own PSPLIM guard machinery on CM33.
- Kconfig for both hooks. ~10–15 lines.
- `fault.c`: NOT patched — weak `k_sys_fatal_error_handler` override
  implements app-fault-kills-app / kernel-fault-coredumps policy, preserving
  process_manager force-close semantics.
- MPU: `CONFIG_CPU_HAS_CUSTOM_FIXED_SOC_MPU_REGIONS` reserves Pebble's slots;
  Zephyr's dynamic-MPU code untouched.
- Local adapter ~500–900 lines (SVC decode, frame relocation, privileged-stack
  switch, fault classification, tests).
- Upstream attempt (non-blocking): generic `ARM_CUSTOM_SVC_HANDLER` /
  `ARM_CUSTOM_THREAD_CONTEXT_RESTORE` hooks as "application-managed execution
  domains."
- Uprev policy: pin Zephyr, uprev ≤1/year; expected cost 1–3 days rebase +
  5–10 days sandbox regression per uprev (~6–12 engineer-weeks over 3 years).

**Kill criterion (hard):** if the P0 spike needs >75 non-test Zephyr lines,
touches files beyond svc.S/swap_helper.S(+Kconfig), or depends on private
k_thread layout — stop, escalate, reconsider Zephyr userspace.

## 4. Transition model (settled in debate)

**Dual-build integration train — not dual-backend product, not hard-cutover
branch.** (123 firmware commits in 30 days makes a long-lived port branch
indefensible; a Kconfig-selectable OS backend inside one tree was equally
indefensible — two source graphs, two image pipelines.)

- Main keeps shipping FreeRTOS. Only OS-neutral work lands on main: seam
  routing, ABI/contract tests, CTest port, dts/codegen, board metadata —
  zero behavior change, FreeRTOS QA green.
- Zephyr target lives on a short integration branch only until it boots in
  QEMU, then merges into main as a **non-shipping second build target**.
- Agents rebase/integrate the Zephyr target daily; weekly formal integration
  train; humans review semantic conflicts (init order, ownership, blocking,
  IRQ, power).
- Hardware lab tiering: per-PR = smoke only; nightly = regressions; weekly =
  full QA; RC-only = 7-day PPK2 soak (a soak monopolizes the rig).
- Endgame: 2–4 week **narrow** freeze (kernel/drivers/linker/BLE/flash/PM
  paths only); weekly customer releases continue from a FreeRTOS release
  branch; cut main to Zephyr once; keep FreeRTOS branch one emergency release
  cycle; delete.

## 5. Phases and gates

### P0 — Architecture/contract spike (3–5 wk)
- Freeze the compatibility spec: PBW ABI, jump-table layout, calling
  convention, float ABI, struct packing, executable-RAM policy (threat-model
  the W^X exception), syscall failure semantics, force-close/kill/relaunch
  contract, timing tolerances. This becomes a conformance test suite.
- Pin Zephyr version; define ROM/RAM budgets per board; paper MPU maps
  replaced by a **booting hardware prototype on asterix**: SVC/PendSV hooks,
  app+worker unprivileged, callback re-entry, force-close, faults, privileged
  stacks, exact MPU slot usage. Same prototype exercise on SF32 devkit for
  the 16-region/vendor-slot map.
- Gates: old PBWs run unmodified on the prototype; Zephyr patch within kill
  criterion; MPU maps proven on both SoCs with all intended Kconfig on.

### P1 — Mainline-neutral groundwork (3–6 wk, overlaps P0)
- Route 81 direct-FreeRTOS files through extended pbl/os API (swarm-parallel).
- clar → CMake/CTest harness port. Board pin tables → devicetree (interim:
  generate old C tables from dts, diff).
- west workspace + module pins; pblboot board defs imported; `pbl` wraps west.
- Contract tests: init-order, priority/ISR mapping table (FreeRTOS→Zephyr
  priorities, cooperative vs preemptive, PendSV/SVC priority ownership,
  zero-latency IRQ needs for BT controller), ABI suite from P0.
- Gate: FreeRTOS QA stays green; zero behavior change.

### P2 — Zephyr kernel + QEMU integration (4–6 wk)
- Zephyr board defs for pebble-emery/flint/gabbro; pbl/os Zephyr backend;
  9-task bring-up (App/Worker unprivileged via P0 mechanism); PFS on Zephyr
  flash API; PULSE/console on Zephyr uart; loader + process supervision.
- Zephyr target merges to main as non-shipping build; integration train starts.
- Gate: launcher + PBW corpus (store-app batch) run in QEMU; fault/exit/
  force-close semantics match frozen contract; UI-walk screenshot diffs vs
  FreeRTOS baseline clean.

### P3 — Asterix vertical slice (7–10 wk)
- Boot/OTA/rollback/recovery/PRF/MFG early (they own the linker + flash map —
  not deferrable); pblboot chain-load validated.
- Drivers: mainline Zephyr for commodity (gpio/spi/i2c/uart/flash/rtc/adc/
  wdt/entropy); Pebble drivers wrapped on Zephyr buses for display/backlight/
  touch/imu/hrm/mic/vibe; dts is pin/bus truth.
- PM as first-class project: port nrf52 sleep policy onto Zephyr PM hooks with
  per-phase power budgets (waveforms measured from first boot, not end soak).
- NimBLE: npl_os_zephyr + init-path task creation + HFXO/sleep coordination
  (2–3 wk) then soak; coredump decision executed (Pebble writer ported OR
  Memfault format + tooling update — one, not both).
- Fault injection: flash erase interruption, OTA power loss, app fault during
  privileged call, coredump-during-fault.
- Gate: existing PBWs install/run; OTA rollback proven; pairings retained;
  ROM/RAM/power within budgets; coredump round-trip with tooling.

### P4 — Asterix parity + cutover (5–8 wk)
- Power tuning against scenario budgets (named scenarios, absolute mA limits,
  repetitions, confidence bounds — not "within 5%"); BLE reconnect matrix +
  soak stats; full app-corpus + release QA; 7-day PPK2 soak at RC.
- 2–4 wk narrow freeze → cut main to Zephyr; FreeRTOS release branch kept for
  one emergency cycle.
- Gate: soak + zero PBW ABI regressions + release shipped on Zephyr.

### P5 — SF32LB52 boards: getafix + obelix (10–16 wk, overlaps late P3/P4)
- Shared SoC layer: LCPU IPC + vendor BT blobs, QSPI (CVE fix backported),
  DMA/cache coherency + non-cacheable memory design, MPU map around vendor
  slots 0–4, clock/rail/LPTIM/GTIMER PM port, controller RAM coredump capture.
- Then per-board: displays (JDI), touch (cst816), sensors.
- Gate: each board independently passes the P3/P4 bar.

### P6 — Retirement (3–5 wk after one stable release cycle)
- Delete FreeRTOS backend + fork + waf firmware build; collapse CI/lab
  matrix; document Zephyr arch patch + annual uprev procedure; contributor
  migration guide. SDK export continues emitting today's waf-based SDK.

## 6. Schedule (debate-settled)

| Milestone | Elapsed |
|---|---|
| Asterix ships on Zephyr | 5–7 months |
| All three boards | 7–10 months |
| FreeRTOS + waf deleted | 8–11 months |

Execution model: AI swarm does breadth (seam routing, dts conversion, driver
wrapping, test scaffolding, daily rebases — weeks not months); 1–2 senior
humans are the review bottleneck for semantics (MPU, PM, BLE, security);
single hardware lab is the serialization point (tiered usage above).
Irreducible: silicon debugging, power waveform analysis, BLE/power soak
wall-clock, security sign-off.

## 7. Risks

| Risk | Mitigation |
|---|---|
| P0 spike breaches kill criterion | Explicit stop + reconsider userspace; decided in week 5, not month 6 |
| nRF 8-region MPU doesn't fit | P0 hardware prototype with all Kconfig on; fallback: consolidate fixed mappings, shrink guard usage |
| Power regression | Per-phase budgets from first boot; PM ported as SoC project; RC soak gate |
| SF32 platform youth (CVE class bugs, LCPU coupling) | Asterix first; CVE backports to pin; early P5 spike on devkit during P3 |
| Integration-train drag (semantic drift between builds) | Contract tests on main; daily agent rebases; weekly human train review |
| Force-close/lifecycle regressions | Contract frozen in P0 as tests; fault injection in P3 |
| Coredump tooling break | Single deliberate choice in P3 with tooling parity test |
| npl/BLE latency or HFXO regressions | NimBLE unchanged above npl; soak early; controller timing in contract tests |
| Zephyr uprev pain (arch patch) | Pin + ≤1 uprev/yr; upstream hook proposal in flight; kill criterion bounds patch size |
| Rig contention | Tiered lab policy; soak only at RC |

## 8. Open questions (remaining)

1. Zephyr pin: newest LTS vs 4.5 (SF32 QSPI fix native)? rec: pin the LTS,
   backport SF32 fixes; revisit at P5.
2. task_timer/NewTimers: keep own task (rec) vs k_work_delayable — decide by
   prototype diff in P2.
3. Coredump: port Pebble writer (rec, keeps readcore.py/toolchain) vs adopt
   Memfault/Zephyr format — decide in P3 planning.
4. Upstream Pebble QEMU board defs + arch hooks to zephyrproject-rtos? rec:
   propose hooks, keep boards in-tree.
5. clang/LLVM firmware builds on Zephyr (LLVM toolchain variant) — revisit
   after P6; Phase-0 hygiene from the LLVM audit lands regardless.
