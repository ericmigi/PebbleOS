# P3 BLE — handoff (blocked on LCPU-core SWD)

## One-line state
BLE host stack up on Zephyr; SF32 controller HCPU-side bring-up 100% correct;
**blocked solely because the LCPU radio core never executes its silicon ROM.**
Every HCPU-observable input verified correct. Remaining diagnosis needs SWD on
the LCPU core. Everything downstream (advertise -> CoreApp connect -> notification
-> panel render) is ready; the notification render path is already green (P2 notif app).

## What is proven correct (do NOT re-investigate)
- Shared RAM mapped: HCPU write-readback of 0x20402800 / 0x20400000 echoes exactly.
- Patch installed: valid Thumb @0x2040500C, "PACH" header @0x20405000 (rev_b, correct for revid 0x0f).
- Config table intact: magic + HCPU_TX_QUEUE=0x2007FE00 @0x20402A00 (rev_b MB_CH2 addr).
- Reset released: pmr=0 cpuwait=0 rstr1=0. LCPU not halted, not in reset.
- Crystal up BEFORE bring-up: acr_hp/acr_lp=0xC0000007, lp_hxt48_rdy=1 (Zephyr clock init does this).
- Doorbell delivered: H2L TX mailbox CxISR=1 CxMISR=1 (unmasked, asserting to LCPU).
- IRQ 58 (LCPU2HCPU) wired + enabled.
- lcpu_power_on() (shipping SDK, identical to FreeRTOS obelix) runs fully incl HAL_RCC_ReleaseLCPU + HAL_LPAON_ConfigStartAddr + patch + 5ms delay.
- SystemInit "gap" is a red herring: boot_images/hw_preinit0/SystemPowerOnModeInit are __WEAK empty stubs in shipping too; SystemInit's only real work (VTOR/CPACR/FPU/cache) Zephyr already does.

## The smoking gun
Zeroed the LCPU->HCPU RX ring control at 0x20402800 immediately before ReleaseLCPU;
post-boot it STAYS zero. Per ipc_queue.c:212 the RX ring is initialized by the LCPU
(sender). Zero after boot => the LCPU never ran even its own IPC init => it hangs in
early silicon-ROM boot before touching the HCPU. (Prior "garbage" reads were stale
LPSYS retention RAM; the 1.2s PPK2 power cycle does not clear retention.)

## Next step: SWD on the LCPU core (needs rig hardware)
Goal: read the LCPU core's PC / fault status at and after HAL_RCC_ReleaseLCPU() to see
why the ROM won't start. LCPU is a separate Cortex-M core with its own SWD.
1. Wire a debug probe (J-Link / DAPLink) to the obelix LCPU SWD pads (not the HCPU
   SWD used for flashing). Identify pads from the obelix schematic / SF32LB52 datasheet.
2. Halt the LCPU, read PC/xPSR/CFSR after release. Expected findings to distinguish:
   - PC stuck at a fixed ROM addr / hardfault -> ROM boot precondition unmet (config/security/eFuse).
   - PC in WFI/sleep -> LCPU booted then parked waiting a wake the HCPU must issue.
3. Cross-check against a live shipping FreeRTOS obelix unit if a second probe is available.

## Cheap control experiment (optional, decision-relevant, autonomous-doable)
Flash shipping FreeRTOS obelix firmware to THIS exact unit and confirm its BLE
controller reaches "up" (LCPU boots) in its console. If yes -> the port is the sole
variable, SWD-on-port is right. If no -> this unit's LCPU may be unhealthy
(eFuse/patch/silicon), which would redirect the whole effort. Build per RUNBOOK sec1
(./pbl configure --board obelix@pvt && ./pbl build), flash pebbleos.hex, watch console.

## Reproduce the current BLE probe build
cd ~/dev/pblboot-ws && export ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb GNUARMEMB_TOOLCHAIN_PATH=$(dirname $(dirname $(which arm-none-eabi-gcc)))
.venv/bin/west build -b pt2 ~/dev/pebbleos-zephyr/zephyr-port-apps/ble -d ~/dev/pebbleos-zephyr/build-ble
# flash+capture: scp zephyr.hex to unicorn-mac-1:obelix-flash/shellz/ble.hex; step.py write_flash; boot_capture.py 1000000 14 | grep BLE_
# diagnostics printk'd: BLE_REVID, BLE_PATCH_OK, BLE_RF_CAL_OK, BLE_LPRAM_RB, BLE_RING_CTRL, BLE_TX_SIGNALED, BLE_LCPU_RUNSTATE, BLE_HCI_TX, (BLE_HCI_IRQ never appears = the bug)
