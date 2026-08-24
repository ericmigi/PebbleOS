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
