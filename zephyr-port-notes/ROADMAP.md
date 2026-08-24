# PebbleOS → Zephyr port: full roadmap + current position

_Obelix-first. Legend: [x] done on hardware · [~] partial/scaffolded · [ ] not started._

## The honest build-status headline

What exists today is **real PebbleOS source code compiled against Zephyr through a
bespoke scaffold** — a set of hand-written Zephyr apps under `zephyr-port-apps/`
(`kernel`, `gfx`, `loader`, `watchface`, `watchface_sandboxed`, `pfs`, `fw`), each
`CMakeLists` pulling selected real PebbleOS `.c` files and building them with `west`.
The `fw/` app consolidates them into one image. This is NOT yet the real PebbleOS
build producing the shipping firmware: peripheral services are stubbed
(`FW_STUB display_compositor / ble_comm / watchdog_analytics / board_drivers`), the
launched app is an embedded PBW blob (not the real AppDB code bank), and waf still
builds the actual FreeRTOS firmware. The scaffold proves every mechanism on real
hardware; converting it into "the PebbleOS tree builds a Zephyr firmware" is the
main remaining lift (P3–P6).

## Phase map

### P0 — architecture/contract spike  [x] DONE
- [x] Pebble MPU/SVC sandbox rides on Zephyr threads (svc#4 hooks, MPU fault
  containment) — 57-line arch patch, under the kill criterion, green on obelix.
- [x] Real hardware MPU map captured (12 regions, 10 free for Pebble).
- [ ] Frozen PBW ABI conformance suite, exact per-board ROM/RAM budgets, force-close
  contract as tests (deferred; the mechanism is proven, the formal spec is not).

### P1 — mainline-neutral groundwork (FreeRTOS stays green)  [x] DONE
- [x] `pbl/os` Zephyr backend (mutex/sem/tick), real PebbleOS util code, 7/7 on hw.
- [x] FreeRTOS seam audit + first routing; FreeRTOS obelix build still green.
- [x] Zephyr boots on obelix (`pt2` board), shell, external QSPI NOR read.
- [x] nPM1300 PMIC, Zephyr PM contract (idle-power datapoint).
- [~] clar unit-test harness → CMake/CTest: not done (waf tests still).

### North Star (vertical-slice demo)  [x] DONE
- [x] Real published Sliding Text PBW builds/loads, runs, renders real Gotham font,
  upright, correct time, ON THE PHYSICAL PANEL under Zephyr.

### P2 — real PebbleOS model on Zephyr (as a consolidated scaffold)  [x] CORE DONE
- [x] P2.1 unprivileged sandbox: real PBW runs unprivileged in the MPU sandbox,
  survives preemption, ticks, renders (hardware).
- [x] P2.2 consolidation: unified `fw` boots the real task/service model as Zephyr
  threads (KernelMain/KernelBackground/NewTimers, event loop, tick, timers).
- [x] P2.3 PFS: real filesystem on the QSPI NOR, integrated into `fw`.
- [x] P2.4 registry/launcher: real app registry from PFS (26 system apps),
  launcher selects + launches.
- [x] CAPSTONE: `fw` launches an app that runs sandboxed AND renders on the panel,
  with firmware kernel threads ticking in parallel.
- [~] Still a scaffold: services stubbed, embedded-PBW not AppDB code bank,
  display/compositor not a first-class fw service (app pushes frames directly).

### P3 — obelix hardware vertical slice → a real firmware  [ ] NEXT
- [ ] Real AppDB code-bank load (install a PBW to PFS, launch it from storage — not
  an embedded blob).
- [ ] Display/compositor + input (buttons/touch) as first-class fw services;
  the real launcher UI + window stack, navigable.
- [ ] BLE: port NimBLE's npl to Zephyr, SF32 controller, pair with phone, app
  install over Bluetooth.
- [ ] Boot/OTA/recovery/coredump: dual-slot, rollback, PRF, coredump round-trip.
- [ ] Un-stub the FW_STUBs (board_drivers, watchdog, analytics/Memfault).

### P4 — the build convergence + parity + cutover  [ ] BIG LIFT
- [ ] Replace the bespoke `zephyr-port-apps` scaffold with the REAL PebbleOS tree
  building a Zephyr firmware (the actual source tree + build system emit the
  Zephyr image, not hand-curated CMake source lists). This is the "build from
  PebbleOS, not one-off compiles" milestone.
- [ ] Full app-compatibility corpus + hardware-lab QA on the Zephyr image.
- [ ] Power parity (PPK2 soak) + BLE soak within budgets.
- [ ] Narrow freeze, cut obelix to Zephyr, keep FreeRTOS one emergency cycle.

### P5 — remaining boards  [ ] 
- [ ] getafix (SF32LB52, shares SoC work) then asterix (nRF52840, Nordic mainline).

### P6 — retire FreeRTOS + waf  [ ]
- [ ] Delete the FreeRTOS backend + fork; waf retired for firmware; CI/lab collapse.

## Where we are, in one line
Every mechanism is proven and a meaningful subset is integrated + running on obelix
hardware (through P2), but as a **scaffold of real code**, not the real firmware
build. The next milestone (P3) hardens it toward a wearable; the "build the whole
PebbleOS tree on Zephyr" convergence is P4.
