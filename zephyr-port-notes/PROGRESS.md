# Zephyr-on-obelix port — overnight run log

Branch: `zephyr/obelix` (worktree). Plan: scratchpad zephyr-port-plan.md (v2, post-Codex-debate).
Rig: unicorn-mac-1 (100.87.44.126), obelix PVT on PPK2, ppk2d HTTP :8843, flash via `step.py` (known-good).

## Milestones

- **M1 DONE — Zephyr boots on obelix hardware.** `zephyr/samples/hello_world`, board `pt2`
  (coredevices/zephyr fork, branch `pblboot`, Zephyr 4.3.99), built with gnuarmemb
  (arm-none-eabi-gcc 14.2, no Zephyr SDK needed), flashed at bootloader slot 0x12010000
  via step.py. UART1 @1M: `*** Booting Zephyr OS build 31a509631933 *** / Hello World! pt2/sf32lb52jud6`.
- **M2 DONE — interactive Zephyr shell on watch.** shell_module sample: scheduler, threads,
  `device list` (clocks/gpio/pinctrl READY). TX needs ~10ms/char pacing (no flow control).
- **M3 DONE — power datapoint.** Zephyr shell idle, zero PM config: **3.38 mA @ 3.8V**
  (ppk2d /measure, 4s avg). Reference for later PM work, not a parity claim.
- M4 IN FLIGHT — P0 sandbox spike (sol): custom SVC + thread-restore hooks in
  arch/arm (≤75 lines), unprivileged thread + svc#4 elevation + MPU fault containment
  demo on pt2. Kill criterion per plan §3.
- M5 IN FLIGHT — pbl/os Zephyr backend (sol): mutex/tick on k_*, real PebbleOS util
  code compiled into a Zephyr app, self-test suite over UART.

## Rig facts learned

- ppk2d.py daemon (fwtest/firmware) is the ONLY allowed PPK2 owner; HTTP :8843
  /status /on /off /cycle?off_ms= /measure?seconds=. Never open the PPK2 serial directly —
  a second holder corrupts the stream (killed stale PID tonight).
- boot_capture.py + shell_probe.py uploaded to ~/obelix-flash/ (UART-only, cycle via ppk2d).
- Baseline before work: watch showed only `SFBL` (no bootable bootloader after QA runs).
  pblboot restore assets on rig: pblboot-pt2-v0.9.21.hex + ftab + PRF (untouched).

## Toolchain facts

- west workspace: ~/dev/pblboot-ws (manifest: pblboot/west.yml → coredevices/zephyr@pblboot,
  hal_sifli, cmsis_6). Boards in fork: coredevices/pt2 (obelix), p2d. pblboot repo itself
  only carries pr2 (getafix) now — pt2 moved in-tree into the fork.
- gnuarmemb variant works: ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb GNUARMEMB_TOOLCHAIN_PATH=<brew arm dir>.
- SF32LB52 SoC Kconfig selects CPU_HAS_ARM_MPU. No JDI display driver in fork (only ls0xx) —
  obelix display bring-up = port PebbleOS display_jdi.c to a Zephyr driver (future task).

- **M6 DONE — external QSPI NOR reads under Zephyr.** CONFIG_FLASH + CONFIG_FLASH_SHELL +
  CONFIG_FLASH_SF32LB_MPI_QSPI_NOR; flash device name is the mpi2 controller node
  ("memory-controller@50042000" — the gd25q256e child is not a separate device).
  `flash read ... 10000 30` returns the flashed Zephyr vector table. DMA controller READY.
  Write/erase deliberately untested (would eat our own image); PFS bring-up unblocked.

- **M7 DONE — pbl/os Zephyr backend verified on hardware, 6/6.** lib/os/{mutex,tick,platform}_zephyr.c
  implement include/pbl/os on k_mutex/k_*. zephyr-port/ app compiles REAL PebbleOS OS-free code
  (lib/util circular_buffer + crc32) against the seam. On obelix UART:
  SMOKE_PASS x6 (mutex lock/unlock, recursive, contention; tick; circular_buffer; crc32), SMOKE_DONE 6/6.
  Six FreeRTOS-vs-Zephyr semantic deltas documented in commit — notably: Zephyr mutexes always
  recursive (adapter blocks recursive PebbleMutex); Zephyr adds +1 tick to relative timeouts
  (adapter uses K_TICKS(N-1)); pt2 Zephyr tick = 10kHz vs Pebble SF32 FreeRTOS 1kHz.
- **M8 DONE — sandbox spike hooks fit the kill criterion.** arch/arm custom SVC(#4) + thread-restore
  hooks: 57 lines added to existing Zephyr arch files (Kconfig 16, swap_helper.S 17, svc.S 23, CMake 1)
  — under the plan's 75-line stop-gate. CONFIG_USERSPACE=n. On hardware: MPU reports 12 DREGIONs
  (not 8 — room for Pebble's task regions). svc#4 elevation + MPU fault containment still being
  tuned for XIP-from-external-flash (app code executable region on this SoC). Committed on the
  zephyr fork branch pebble-sandbox-spike.

- **M8 COMPLETE — P0 sandbox spike FULLY GREEN on obelix hardware.** The single biggest
  de-risk of the whole port. On real pt2 silicon:
  `PASS_SYSCALL 20` (unprivileged app thread issues svc#4, custom hook elevates, reads
  k_uptime, returns unprivileged) / `PASS_FAULT_CONTAINED reason=19` (app reads guarded
  buffer, MemManage fault caught by overridden k_sys_fatal_error_handler, only the app
  thread aborted) / `PASS_KERNEL_ALIVE` x9 (kernel + main thread survive).
  Pebble's foreign MPU/SVC sandbox proven riding on Zephyr threads, CONFIG_USERSPACE=n,
  57 lines added to existing Zephyr arch files (< 75-line kill criterion). Branch
  pebble-sandbox-spike on the zephyr fork.
  HARDWARE MPU MAP (deliverable): 12 DREGIONs. Region 0 = XIP flash @0x12000000 AP3 RO+X
  (unpriv-executable, no code slot needed). Region 1 = SRAM @0x20000000 AP0 priv-RW.
  Regions 2-11 free for Pebble task regions. MAIR0 0x0044ffaa.
  Key fork finding: Zephyr clears MMFSR before k_sys_fatal_error_handler, so fault-PC
  match is the robust contained-fault signal (MMARVALID/MMFAR unreliable there);
  reason 19 == K_ERR_ARM_MEM_DATA_ACCESS on this fork.

- **M9 DONE — nPM1300 PMIC battery/charger readback on hardware.** Mainline Zephyr
  nPM1300 driver (CONFIG_MFD_NPM13XX + CONFIG_NPM13XX_CHARGER, sensor API), no dts changes.
  On obelix UART: PMIC_VBATT 3808 (matches PPK2's 3800mV supply), PMIC_TEMP 25319 (25.3C
  room temp), charger online=0 (correct, PPK2-powered not USB), PMIC_READ_OK. Battery/charge
  sensing proven under Zephyr. Sample samples/pt2_npm1300 on the zephyr fork.
- **M10 DONE — FreeRTOS seam audit + first routing (Z0.1).** zephyr-port-notes/FREERTOS_SEAM_AUDIT.md.
  Mechanical: 109 sem/mutex, 51 queue, 9 delay/yield, 4 task create. Design-first: 59
  critical-section, 52 TCB, 15 ISR. None: event-groups/sw-timers/notifications. Added
  include/pbl/os/semaphore.h (both backends), routed reliable_transport.c as proof.
  NOTE: FreeRTOS obelix build regression check still pending (rig/worktree venv setup).

- **M11 DONE — FreeRTOS obelix build PASSES with seam routing (regression gate green).**
  Full `./pbl configure --board obelix@pvt && ./pbl build` in the worktree: build/pebbleos.hex
  produced, 2071 targets clean, FLASH 64.89% / KERNEL_RAM 68.36%. Confirms the pbl/os seam
  routing (17 semaphore sites through pbl/os, portMAX_DELAY->UINT32_MAX in bt_lock, tick.h
  __ZEPHYR__ guard, *_zephyr.c excluded from link) does NOT regress the shipping FreeRTOS
  firmware. The inviolable "never break FreeRTOS" rule holds.
  Worktree is now a self-contained build env: real .venv (pip -r requirements.txt) + all
  submodules initialized. (Gotcha logged: symlinking submodules from main then rm-ing them
  deletes superproject glue wscript_build/config files alongside — restore with git checkout.)

- **M12 DONE — Zephyr PM contract on SF32LB52 + power datapoint.** SoC had no HAS_PM;
  added HAS_PM + PM_STATE_RUNTIME_IDLE soc hook + pt2_lowpower sample. On hardware:
  BOOT PM=1 PM_DEVICE=1 TICKLESS=1; idle current 3.38mA (unchanged from baseline — the
  baseline already used tickless SysTick + WFI, and runtime-idle also just WFIs with clocks
  retained). Win = PM path now exists; real savings need deep sleep (LPTIM/RTC wakeup,
  DLL clock switching, QSPI/XIP handling, retention) — deferred, SiFli HAL has the
  primitives but not the Zephyr integration. Branch/sample on the zephyr fork.

## North Star swarm (Sliding Text watchface on obelix)

- **S3 GRAPHICS GREEN ON HARDWARE.** Real PebbleOS applib graphics + text + an embedded
  Pebble PBF font render "12:34" into a 200x228 8bpp (ARGB2222) framebuffer on the actual
  obelix. UART: GFX_CRC 0xa61a348a (byte-identical to host prediction) + ASCII preview
  clearly showing 12:34. Entire graphics ABI path de-risked independent of the display.
  Framebuffer contract for S2 integration: contiguous 45,600 bytes GColor8 ARGB2222
  (AA RR GG BB), 200-byte stride; JDI path converts lower-6 RGB222->RGB332 before DMA.
- **S1 KERNEL on hardware:** KERNEL_UP + real Pebble tick_timer_service fires (one TICK
  landed); fixing repeat + wall-clock advance (RTC frozen at epoch fallback).
- **S2 display:** relaunched after a concurrent-codex launch collision.
- **S4 loader:** in flight.

- **S4 LOADER + SYSCALL GREEN ON HARDWARE.** Real Sliding Text emery binary (3228B) on obelix:
  LOADER_OK (header v16.0/SDK5.95, CRC 0x9a4755c9 valid via real Pebble checksum, 52 relocations
  applied, jumptable patched) + SYSCALL_OK (DEFINE_SYSCALL over svc#4 from unprivileged thread
  returned 42). PBW load path AND sandbox syscall path both proven on the real app binary.
  Production wiring documented (svc2->svc4, .syscall_text island, time()->sys_get_time index 519).
  S1 deps noted: App/Worker k_thread identity, process mem/stack bounds, per-thread syscall LR/SP,
  MPU restore, fault handling, start (segment+entry)|1 after cache maintenance.

### North Star scoreboard (Sliding Text on obelix)
- S1 kernel/tick: TICK fires on hw; repeat+wall-clock fix in flight.
- S2 display (JDI/LCDC): in flight (long pole).
- S3 graphics: GREEN — 12:34 rendered on hw, CRC 0xa61a348a matches host.
- S4 loader+syscall: GREEN — real PBW loads + svc#4 syscall works on hw.
3 of 4 stream mechanisms proven on real hardware.

- **S1 KERNEL/TICK GREEN ON HARDWARE.** Fixed tick repeat (timer re-arms) + wall-clock advance
  (SF32LB RTC). On obelix: KERNEL_UP then TICK advancing once per second with real time
  (unix 1787581010, 14:16:50->14:16:56). The exact tick_timer_service path Sliding Text uses.
  ALL 4 STREAM MECHANISMS NOW PROVEN except S2 display. Sliding Text's 41 SDK imports decoded:
  tick_timer_service_subscribe + TextLayer + pbl_override_time.

- **S2 DISPLAY GREEN ON HARDWARE.** First Zephyr JDI/LCDC driver for obelix. On the watch:
  DISPLAY_BIND ok / DISPLAY_WRITE_START / DISPLAY_EOF / DISPLAY_DONE — the LCDC pushed a full
  frame to the real panel and the EOF callback fired (no silent-loss timeout). SiFli HAL reused,
  full pt2 dts (LCDC1 @0x50008000, panel + 12 JDI signals). Committed on zephyr fork branch
  pt2-display. Pixels not eyeball-verified (no rig camera) but frame transfer completes.

### ★ ALL FOUR NORTH STAR STREAM MECHANISMS GREEN ON REAL OBELIX HARDWARE ★
- S1 kernel/tick: TICK advances 1/sec with real wall clock.
- S2 display: JDI/LCDC frame transfer + EOF on the panel.
- S3 graphics: real PebbleOS pipeline renders 12:34, CRC matches host.
- S4 loader+syscall: real Sliding Text PBW loads (52 relocs, CRC valid) + svc#4 syscall.
Remaining: S5 integration (fuse into the running watchface) + final S3-framebuffer -> S2-display_write
wiring for the visible ticking clock.

## ★★★ NORTH STAR (compute): REAL SLIDING TEXT WATCHFACE EXECUTES ON OBELIX/ZEPHYR ★★★
S5 watchface capstone flashed to real obelix. UART:
  WATCHFACE_LOADED entry=0x000005b8 reloc=52   (real PBW binary loaded + relocated)
  WATCHFACE_PATH PBW_PRIVILEGED                 (REAL PBW execution path, not the fallback)
  WATCHFACE_UP                                  (the app's own init ran + returned)
  WATCHFACE_TICK 14:44                          (its tick handler fired, real time)
  WATCHFACE_FRAME 0x5e882aa5 + ASCII preview showing rendered glyphs
The unmodified published third-party Sliding Text watchface's OWN machine code runs on
PebbleOS-on-Zephyr on real obelix hardware, subscribes to tick_timer_service, and renders
the time into the 200x228 framebuffer. Every North Star mechanism proven AND integrated.
Remaining: wire the framebuffer -> S2 display_write (RGB222->RGB332) so it's visible on the
physical JDI panel = the final pixel-push. Caveats: runs privileged/MPU-off (S4 proved the
unprivileged svc#4 sandbox separately); substituted system fonts (original app_resources.pbpack
not embedded); synchronous animation adapter.

## ★★★★★ NORTH STAR ACHIEVED — REAL WATCHFACE VISIBLE ON THE OBELIX PANEL ★★★★★
S6 wired the watchface framebuffer -> S2 display_write (DEVICE_DT_GET(DT_CHOSEN(zephyr_display)),
display_blanking_off, display_write 200x228 pitch200 45600B, PIXEL_FORMAT_L_8 carrier, driver does
ARGB2222->RGB332). Flashed to real obelix: the unmodified published Sliding Text watchface's OWN
PBW code runs on PebbleOS-on-Zephyr, ticks, and renders the time as words ("three"/"two") VISIBLE
ON THE PHYSICAL JDI PANEL (photo: zephyr-port-notes/sliding-text-on-obelix.png). Display driver
also independently confirmed with color-bar test (colorbars-on-obelix.png).
Full path proven end to end on hardware: PBW load + relocate + jump-table -> app runs in process
context -> tick_timer_service -> applib graphics/text/font -> framebuffer -> JDI/LCDC panel.
Known cosmetic follow-ups: panel mounted 180deg (needs orientation flip), substituted system fonts
(original app_resources.pbpack not embedded), runs privileged (S4 proved unprivileged svc#4 sandbox
separately), UART log spam from CONFIG_LOG preview.

## PHASE P2 (real PebbleOS on Zephyr) — progress
- **P2.2 CONSOLIDATION GREEN on hardware.** Unified firmware zephyr-port-apps/fw/ boots the
  REAL PebbleOS task/service model as Zephyr threads: FW_BOOT, real tasks up (KernelMain/
  KernelBackground/NewTimers), FW_EVENT_LOOP_UP, FW_SERVICES_OK, FW_TICK advancing, FW_TIMER
  dispatched. Stubs: board_drivers, display_compositor, pfs_resources, ble_comm, app_worker,
  watchdog_analytics. Committed aad0e13e5.
- **P2.3 PFS GREEN on hardware.** Real PebbleOS PFS (pfs.c + flash_translation + flash_region)
  on Zephyr over the QSPI NOR via a flash shim (scratch region 0x13e00000, safe). On obelix:
  PFS_MOUNT_OK / PFS_WRITE_OK 257 / PFS_READ_OK 57ed1a9f / PFS_PERSIST_OK / PFS_DONE. Committed 810a23970.
- **P2.1 SANDBOX progressing.** Unprivileged sandboxed watchface: SANDBOX_APP_UP + SANDBOX_SYSCALL_OK
  (app runs unprivileged, first svc#4 syscall elevates+returns) on hardware; then FATAL ERROR 20
  after the first syscall — post-syscall step under diagnosis. (Boot-assert root cause was
  tick_timer_service_init before app-thread registration; moved into a privileged svc#4 call.)
  NOTE: touches src/fw/system/passert.h (build-gated WTF context) — verify FreeRTOS build unaffected.

- **P2.1 SANDBOX COMPLETE on hardware.** The real Sliding Text PBW runs UNPRIVILEGED in the
  Pebble MPU sandbox on obelix, survives timer preemption (MPU-swapping context switch), ticks,
  and renders: SANDBOX_APP_UP / SYSCALL_OK / app_entry / window_create / text_layer_create /
  tick_subscribe / event_loop / wait_tick / tick_handler / render / SANDBOX_TICK 18:13 /
  SANDBOX_FRAME, no fatal, kernel alive. Root causes (fixed directly, codex was down): (1) the
  firmware-code MPU region left execute-never (XN not cleared); (2) per-write ISB during
  multi-region MPU reprogram forced a refetch under a transiently inconsistent map -> fault at
  the first preemption; batch the writes with the MPU disabled, then re-enable. Committed 487a77405.
  Also fixed 3 stale bisect artifacts that had broken the build/boot (dup main in boot_probe.c,
  SANDBOX_BISECT_NO_SYSCALL_SECTION define, early return() truncating CMakeLists) + added
  CONFIG_LOG_MODE_IMMEDIATE so markers survive a crash.

### P2 scoreboard
- P2.1 unprivileged sandbox: DONE (real PBW runs sandboxed on hw).
- P2.2 consolidation: DONE (real task/service model boots on hw).
- P2.3 PFS: DONE (filesystem on QSPI NOR) + integrated into the unified fw.
- P2.4 app registry/launcher: in flight.
- Remaining: display/compositor into fw; capstone = launcher runs a stored PBW sandboxed.

## ★★★★★ P2 CAPSTONE COMPLETE — FIRMWARE LAUNCHES AN APP SANDBOXED, ON THE PANEL ★★★★★
The unified PebbleOS-on-Zephyr firmware (zephyr-port-apps/fw/) on real obelix:
FW_BOOT -> real tasks up -> FW_SERVICES_OK -> FW_PFS_MOUNT_OK -> FW_REGISTRY_UP ->
FW_LAUNCH TicToc -> SANDBOX_APP_UP -> SANDBOX_SYSCALL_OK -> SANDBOX_TICK/SANDBOX_FRAME,
with FW_TICK advancing in parallel. The launched app runs UNPRIVILEGED in the MPU
sandbox and RENDERS ON THE PHYSICAL PANEL ("seven thirty one" = 19:31, real Gotham) —
photo zephyr-port-notes/capstone-app-sandboxed-on-panel.png. Firmware kernel threads and
the sandboxed app coexist. Committed 563c40bd3.
Empty-PFS/no-AppDB made non-fatal (falls through to launch the embedded PBW).

### P2 (real PebbleOS on Zephyr) — ALL CORE SLICES DONE ON HARDWARE
- P2.1 unprivileged sandbox: DONE.  P2.2 consolidation: DONE.  P2.3 PFS: DONE.
- P2.4 registry/launcher: DONE.  CAPSTONE (launch app sandboxed under full fw): DONE.
Remaining polish: real AppDB code-bank load (currently embedded PBW), display/compositor
as a first-class fw service (the app pushes frames directly today), BLE/OTA (P3).

## P3 BLE — root-cause narrowed (2026-08-24)
Controller HCPU-side bring-up all succeeds: BLE_REVID 0x0f, patch REV_B (correct
for 0x0f per bf0_lcpu_init.c:96-100), RF cal OK, IRQ 58 wired, host TX's HCI
Reset (0c03) into H2L ring 0x2007fe00 (write_idx=4). BUT the LCPU core is SILENT:
- TX ring read_idx stuck 0 (LCPU never drains our reset)
- s_lcpu_irq_count stays 0 (LCPU never fires IRQ 58 back) — no BLE_HCI_IRQ, no sync/RX
- RX ring 0x20402800 (rev_b) reads garbage; legacy 0x20405c00 all-zero
lcpu_power_on() IS called (transport:598) incl HAL_RCC_ReleaseLCPU + patch + 5ms delay.
=> LCPU released+patched from HCPU view but not executing its BLE loop.
PRIME HYPOTHESIS (agent a9270f8 testing): LPSYS/LCPU shared RAM 0x2040_0000
(patch+config+RX ring live here) not reserved/mapped in pt2 board memory config,
so HCPU writes never reach the RAM the LCPU boots from. Decisive test: HCPU
write-readback of 0x20402800/0x20400000 after patch install. If RB fails -> reserve
0x2040_0000 in pt2 dts+linker (match shipping obelix). If RB ok -> rom_config /
release-reset-status / mailbox ordering.

## P3 BLE — shared-RAM hypothesis DISPROVEN (2026-08-24, agent probe)
Added an HCPU write-readback + region-dump probe in hci_sf32lb52.c right after
lcpu_power_on() returns. Result on hw (obelix revid 0x0f):
- BLE_LPRAM_RB 0x20402800 and 0x20400000: 0xA5A5A5A5/0x5A5A5A5A echo perfectly.
- BLE_PATCHCODE 0x2040500C: valid Thumb (0xf000b580 push{r7,lr}+bl), header
  "PACH" 0x48434150 at 0x20405000, readback+restore OK.
- BLE_LCPU_CFG_TBL 0x20402A00 (rev_b config, = LCPU2HCPU_MB_CH2_BUF_REV_B):
  magic=0x45457878, HCPU_TX_QUEUE@off200=0x2007FE00 — correct & intact.
=> LPSYS shared RAM IS mapped/writable from HCPU (incl 0x20405000, above the
   rev_b 11KB window). Patch code, patch header, and LCPU config table ALL land
   correctly. The shared-RAM-mapping hypothesis is WRONG. NOT a dts/linker fix.

Also ruled out:
- Reset release: BLE_LCPU_RUNSTATE pmr=0 cpuwait=0 rstr1=0 => LCPU is released,
  not halted, not in reset.
- LCPU is ALIVE: slp_ctrl=0x60 = XTAL_REQ(bit5)+BT_WKUP(bit6), SLEEP_STATUS clear.
- Doorbell delivered: BLE_TX_SIGNALED tx-mailbox CxISR=1 CxMISR=1 (unmasked,
  asserting to LCPU) but stays latched — the LCPU never clears/services it.

Remaining: LCPU is released, correctly patched+configured, and the HCPU doorbell
reaches it, yet it never drains the H2L ring or fires IRQ 58. Strongest lead:
slp_ctrl XTAL_REQ is asserted by the LCPU (it wants the 48MHz crystal). The
Zephyr port hand-rolls the HAL_PreInit clock/PMU bring-up in
prv_prepare_lcpu_clock() (hci_sf32lb52.c:~120-221) because Zephyr never calls the
vendor board HAL_PreInit that the shipping FreeRTOS obelix runs. Next: compare the
HP<->LP crystal/wake grant handshake (HXT48 enable+ready, LCPU XTAL_REQ grant)
between shipping obelix PM path and this hand-rolled sequence — do NOT invent BLE
logic, mirror shipping. (b)(a) done; pursue (c) mailbox/PM-handshake ordering.
