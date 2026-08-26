# Zephyr-over-OTA — self-serve runbook

How to reproduce, by hand, the two things that were verified on the obelix rig:

- **Test A** — install the Zephyr port onto a stock FreeRTOS/PRF watch, over the air, via CoreApp (no re-pair).
- **Test B** — have a running Zephyr watch self-update from a `.pbz` over its own BLE (A/B slot switch).

No firmware signing is involved — pblboot on this board is CRC-only.

Source: `fork/zephyr/obelix @ c65cc9913` (ericmigi/PebbleOS). Status/design:
`zephyr-port-notes/OTA-PBZ-STATUS.md`, `docs/superpowers/specs/2026-08-25-zephyr-ota-pbz-design.md`.

---

## 0. Prebuilt artifacts (this directory: `~/dev/zephyr-ota-artifacts/`)

| File | What it is |
|------|-----------|
| `zephyr-obelix-slot0.pbz` | Zephyr slot-0 firmware bundle for **Test A** (dev priority ~now). |
| `zephyr-selfota-new.pbz` | Same firmware, **higher** priority timestamp, for **Test B**. |
| `selfota_slot0.bin` | Headered slot-0 image with an **old** priority, to sftool-flash as the Test B receiver. |
| `zephyr_gattfix.hex` | Headered slot-0 image (self-addressed hex) — flash directly with sftool to just boot Zephyr without any OTA. |
| `pblboot_standalone.py` | Copy of `tools/waf/pblboot.py` that runs outside the repo (avoids a `gettext.py` name clash). Packs the pblboot header. |

You can run both tests with just these files — no rebuild needed. Section 5 shows how to rebuild from source.

---

## 1. The rig

- Host: `unicorn-mac-1` (Tailscale `100.87.44.126`), password `pebblepass`. Pixel 7a on its USB, obelix watch on PPK2 power + UART. Everything lives in `~/obelix-flash/` on the rig.
- SSH (password only; you MUST disable pubkey or macOS locks you out after a few keys):
  ```bash
  sshpass -p pebblepass ssh -o PubkeyAuthentication=no -o PreferredAuthentications=password unicorn-mac-1@100.87.44.126 '<cmd>'
  ```
  scp needs `-O`. A transient "Permission denied" = sshd rate-limit; wait a few seconds and retry.
- Flash workhorse: `python3 step.py <sftool-subcommand>` (PPK2 power-cycles into the ROM window for you). **Always** pass `--connect-attempts 0` and read the flash back — sftool can exit 0 on a missed program.
- Console bauds: **pblboot = 115200**, **Zephyr fw = 1000000**, the ROM "SFBL" banner = 1000000.
- Capture UART: `python3 boot_capture.py <baud> <secs>` (power-cycles + reads). `python3 uartlog.py <secs>` reads without a power-cycle.
- adb on the rig: `~/platform-tools/adb`.

Convenience — put this in your shell so the examples are shorter:
```bash
RIG="sshpass -p pebblepass ssh -o PubkeyAuthentication=no -o PreferredAuthentications=password unicorn-mac-1@100.87.44.126"
RSCP="sshpass -p pebblepass scp -O -o PubkeyAuthentication=no -o PreferredAuthentications=password"
ADB="~/platform-tools/adb"
```

---

## 2. Flash map (obelix, GD25Q256E)

```
0x12000000  ftab
0x12010000  pblboot (stock v0.9.21)
0x12020000  FIRMWARE_SLOT_0 (3 MiB)   <- main fw (Zephyr) header@base, body@+0x1000
0x12320000  FIRMWARE_SLOT_1 (3 MiB)   <- A/B target for self-OTA
0x12a20000  PRF region (loads at +0x1000 = 0x12a21000)
0x13FFF000  SHARED_PRF_STORAGE (the BLE bond; survives an 8 MB erase of 0x12000000)
```

pblboot boots the valid slot with the highest 64-bit header `priority` (dev builds stamp `(0x80<<56)|build_unixtime`).

---

## Test A — FreeRTOS PRF → Zephyr, over the air

Goal: a watch running stock PRF, paired to CoreApp, gets the Zephyr `.pbz` pushed over BLE and boots it — no re-pair.

### A1. Put the watch on a clean PRF slate (once)
```bash
$RIG 'cd obelix-flash && \
  python3 step.py --connect-attempts 0 erase_region 0x12000000:0x800000 && \
  python3 step.py --connect-attempts 0 write_flash obelix-hwv_ftab_v1.0.18.bin@0x12000000 && \
  python3 step.py --connect-attempts 0 write_flash pblboot-pt2-v0.9.21.hex && \
  python3 step.py --connect-attempts 0 write_flash prf_obelix_pvt_v4.30.0.hex'
```
Confirm PRF boots (read pblboot at **115200**):
```bash
$RIG 'cd obelix-flash && python3 boot_capture.py 115200 8' | strings | grep -iE 'bootloader|PRF'
# expect: "PebbleOS bootloader 0.9.21" ... "Loading PRF at address 0x12a21000"
```
Pair the Pixel's CoreApp with the watch now if it isn't already (once). After this the bond lives in SHARED_PRF_STORAGE and survives everything below.

### A2. Stage the pbz where CoreApp can read it
Scoped storage blocks `/sdcard/Download`, so push into CoreApp's own files dir:
```bash
$RSCP ~/dev/zephyr-ota-artifacts/zephyr-obelix-slot0.pbz unicorn-mac-1@100.87.44.126:obelix-flash/
$RIG '~/platform-tools/adb push obelix-flash/zephyr-obelix-slot0.pbz \
      /sdcard/Android/data/coredevices.coreapp/files/zephyr.pbz'
```

### A3. Trigger the sideload dialog and install
```bash
$RIG '~/platform-tools/adb shell am start -n coredevices.coreapp/coredevices.coreapp.MainActivity \
      -a android.intent.action.VIEW -d "file:///sdcard/Android/data/coredevices.coreapp/files/zephyr.pbz" \
      -t application/octet-stream'
```
A "Sideload firmware? Install zephyr.pbz" dialog appears. Tap **Install** on the phone, or headless:
```bash
$RIG '~/platform-tools/adb shell input tap 813 1400'   # Install button on the Pixel 7a (1080x2400)
```
Watch the transfer (phone side):
```bash
$RIG '~/platform-tools/adb logcat -d' | grep -iE 'FWUpdate|PutBytes|resource PutBytes skipped|waiting for reboot'
# expect: progress -> PutBytesCommit -> "No resources to send, resource PutBytes skipped"
#         -> PutBytesInstall -> "Firmware update completed, waiting for reboot"
```

### A4. Confirm it booted Zephyr
The watch reboots itself. Check the boot chain:
```bash
$RIG 'cd obelix-flash && python3 boot_capture.py 115200 6' | strings | grep -iE 'slot0|Loading'
#   -> "slot0 firmware valid (0x12021000, ...)"  "Loading slot0 firmware @ 0x12021000"
$RIG 'cd obelix-flash && python3 boot_capture.py 1000000 12' | strings | grep -iE 'FW_BOOT|FW_PFS_MOUNT|FW_APP_COUNT|FW_BLE_BOND_LOADED|FW_BLE_ADV'
#   -> FW_BOOT / FW_PFS_MOUNT_OK / FW_APP_COUNT 26 / FW_BLE_BOND_LOADED / FW_BLE_ADV
```
CoreApp should reconnect with **no re-pair** and show the watch running `v4.0.0-zephyr`. Done.

---

## Test B — Zephyr self-OTA (A/B to slot1)

Goal: a running Zephyr receives a newer `.pbz` over its own BLE, writes slot1, and pblboot boots slot1.

### B1. Flash the receiver into slot0 with an OLD priority
(So the new pbz in slot1 will out-rank it. Requires pblboot already present from Test A — otherwise redo A1 without PRF, or keep PRF, doesn't matter.)
```bash
$RSCP ~/dev/zephyr-ota-artifacts/selfota_slot0.bin unicorn-mac-1@100.87.44.126:obelix-flash/
$RIG 'cd obelix-flash && \
  python3 step.py --connect-attempts 0 write_flash selfota_slot0.bin@0x12020000 && \
  python3 step.py --connect-attempts 0 erase_region 0x12320000:0x300000'     # slot1 clean
$RIG 'cd obelix-flash && python3 boot_capture.py 1000000 12' | strings | grep -iE 'FW_BOOT|FW_BLE_ADV'
```

### B2. Sideload the NEWER pbz (higher priority)
```bash
$RSCP ~/dev/zephyr-ota-artifacts/zephyr-selfota-new.pbz unicorn-mac-1@100.87.44.126:obelix-flash/
$RIG '~/platform-tools/adb push obelix-flash/zephyr-selfota-new.pbz \
      /sdcard/Android/data/coredevices.coreapp/files/zephyr-new.pbz'
$RIG '~/platform-tools/adb shell am start -n coredevices.coreapp/coredevices.coreapp.MainActivity \
      -a android.intent.action.VIEW -d "file:///sdcard/Android/data/coredevices.coreapp/files/zephyr-new.pbz" \
      -t application/octet-stream'
$RIG '~/platform-tools/adb shell input tap 813 1400'   # Install
```

### B3. Watch the self-OTA on the watch (UART, no power-cycle)
```bash
$RIG 'cd obelix-flash && python3 uartlog.py 60' | strings | grep -aE 'FW_OTA'
# expect: FW_OTA_PUT .../435664 ... 435664/435664
#         FW_OTA_COMMIT calc=0x... expected=0x... MATCH
#         FW_OTA_SLOT1_WRITTEN base=0x12320000 ...
#         FW_OTA_INSTALL_REBOOT
```
(The transfer is stop-and-wait over BLE, ~1-2 minutes for 430 KB.)

### B4. Confirm pblboot booted slot1
```bash
$RIG 'cd obelix-flash && python3 boot_capture.py 115200 7' | strings | grep -iE 'slot0|slot1|Loading'
# expect BOTH slots valid, and: "Loading slot1 firmware @ 0x12321000"
```

---

## 3. Just boot Zephyr without any OTA (sanity)
```bash
$RSCP ~/dev/zephyr-ota-artifacts/zephyr_gattfix.hex unicorn-mac-1@100.87.44.126:obelix-flash/
$RIG 'cd obelix-flash && python3 step.py --connect-attempts 0 write_flash zephyr_gattfix.hex'
$RIG 'cd obelix-flash && python3 boot_capture.py 1000000 12' | strings | grep -iE 'FW_'
```

---

## 4. Restore the watch to shipping FreeRTOS
```bash
$RIG 'cd obelix-flash && \
  python3 step.py --connect-attempts 0 erase_region 0x12000000:0x800000 && \
  python3 step.py --connect-attempts 0 write_flash obelix-hwv_ftab_v1.0.18.bin@0x12000000 && \
  python3 step.py --connect-attempts 0 write_flash pblboot-pt2-v0.9.21.hex && \
  python3 step.py --connect-attempts 0 write_flash prf_obelix_pvt_v4.30.0.hex && \
  python3 step.py --connect-attempts 0 write_flash v424/system_resources.pbpack@0x12620000 && \
  python3 step.py --connect-attempts 0 write_flash v424/firmware_obelix_pvt_v4.24.0_slot0.hex'
```

---

## 5. Rebuild the firmware + repackage a pbz (optional)

Zephyr build (west workspace at `~/dev/pblboot-ws`; source at `fork/zephyr/obelix`):
```bash
cd /Users/eric/dev/pblboot-ws
ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb \
GNUARMEMB_TOOLCHAIN_PATH=/Applications/ArmGNUToolchain/14.2.rel1/arm-none-eabi \
.venv/bin/west build -b pt2 <path-to>/zephyr-port-apps/fw -d <build-dir> --pristine
```
Note: a fresh checkout's `third_party` submodules may be empty — symlink them from a populated
checkout (`/Users/eric/dev/pebbleos-zephyr/third_party/{hal_sifli/SiFli-SDK,nimble,mbedtls,nonfree}`).

Header + bundle (uses the standalone packager copy so `tools/waf/gettext.py` doesn't shadow stdlib):
```bash
PY=/Users/eric/dev/pebbleos/.venv/bin/python3
# headered firmware.bin from the raw body .bin (dev priority = the timestamp you pass):
$PY <path>/zephyr-port-apps/fw/tools/prepend_firmware_header.py \
    <build-dir>/zephyr/zephyr.bin firmware.bin --offset 4096 --timestamp $(date +%s)
# a .pbz for CoreApp:
$PY <path>/zephyr-port-apps/fw/tools/package_pbz.py \
    firmware.bin out.pbz --commit $(git rev-parse --short HEAD) --timestamp $(date +%s)
```
For Test B, build the slot0 receiver with an **older** `--timestamp` than the pbz so slot1 wins.
Flash a raw headered `.bin` to a slot with `write_flash firmware.bin@0x12020000` (slot0) — pblboot
reads the header at the slot base.

The header pblboot validates (28 bytes, pack `<LLQLLL`): magic `0x96f3b83d`, header_length 28,
u64 priority, start_offset `0x1000`, length, crc = **zlib/IEEE CRC-32** over the body.

---

## 6. Troubleshooting

- **Only "SFBL" on UART** → you're reading the wrong baud. pblboot=115200, Zephyr=1000000.
- **sftool "Failed to connect"** → it missed the ROM window; re-run (with `--connect-attempts 0`).
  Never whole-chip `erase_flash` (times out) — use `erase_region`.
- **pblboot says "No valid firmware image"** → the slot header is wrong. Read it back
  (`python3 step.py read_flash /tmp/x@0x12020000:32`) and check magic `3db8f396` and that the CRC
  is zlib (not STM32). Vectors must be at `slot_base+0x1000`.
- **CoreApp EACCES reading the pbz** → don't use `/sdcard/Download`; push into
  `/sdcard/Android/data/coredevices.coreapp/files/` as shown.
- **Phone connects but drops right away (GATT_INVALID_HANDLE)** → that was the pre-fix bug; the
  current build sends a Service-Changed indication so the phone re-discovers. If you see it, you're
  running an old image.
- **ssh "Permission denied"** repeatedly → sshd rate-limit from earlier key attempts; wait ~10 s.
