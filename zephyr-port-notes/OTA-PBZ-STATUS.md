# Zephyr-over-OTA via bootable .pbz — status report

Date: 2026-08-25
Branch (port line): `fork/zephyr/obelix` @ `ad8390f3c`
Design spec: `docs/superpowers/specs/2026-08-25-zephyr-ota-pbz-design.md`
Fork only: push `ericmigi/PebbleOS`, never `coredevices`.

## 1. Goal

Package the obelix Zephyr PebbleOS port as a bootable `.pbz` that:
1. The **stock FreeRTOS PRF** (recovery firmware) installs into the main firmware
   slot over BLE via its existing OTA, and the **stock pblboot** validates and boots.
2. Preserves an existing BLE pairing: a phone bonded under FreeRTOS PRF must connect
   to Zephyr **without re-pairing**.
3. Zephyr can later **self-update** from a `.pbz` over its own BLE.

No firmware signing (pblboot is CRC-only). 1:1 compatible with the stock CoreApp —
no app changes.

## 2. Headline result

**On real hardware (obelix PVT on the unicorn-mac-1 rig): the stock pblboot boots the
Zephyr image from slot 0, Zephyr runs its full stack, and a phone paired under
FreeRTOS PRF reconnects to Zephyr with zero re-pairing — CoreApp reports
`connected=true paired=true encrypted=true`, PPoGATT is up, and the watch shows as
`runningFwVersion=v4.0.0-zephyr`.**

The two remaining items are the *OTA install transport itself* (drive CoreApp → PRF →
slot0) and Zephyr's own OTA-receive path.

## 3. Architecture recap

- Unified firmware app: `zephyr-port-apps/fw` (board `pt2` = obelix). FreeRTOS-shim
  kernel + Zephyr; launcher + 26 app registry entries; PFS on real SiFli QSPI flash;
  NimBLE host + SF32 LCPU controller + PPoGATT now integrated in-image.
- Standalone bring-up apps (reference): `zephyr-port-apps/ble` (NimBLE + notifications),
  `watchface_sandboxed`, `notif`.
- Flash map (obelix, GD25Q256E, `flash_region_gd25q256e.h`):
  - XIP base `0x12000000`
  - ftab `0x12000000`
  - pblboot `0x12010000`
  - **FIRMWARE_SLOT_0 `0x12020000` (3072 KiB)** — main fw (Zephyr) lives here
  - FIRMWARE_SLOT_1 `0x12320000` (3072 KiB)
  - PRF region `0x12a20000` (loads at `+0x1000` = `0x12a21000`)
  - PFS filesystem `0x13e00000`
  - SHARED_PRF_STORAGE `0x13FFF000` (the PRF BLE bond; survives an 8 MB erase of
    `0x12000000:0x800000`)

## 4. The pblboot slot-0 image contract (reverse-engineered + HW-confirmed)

pblboot source lives in the `pblboot` Zephyr module (`/Users/eric/dev/pblboot-ws/pblboot`,
`boot/src/firmware.c`), NOT in this repo — the repo only carries the packager
`tools/waf/pblboot.py`. The header pblboot validates at the slot base:

```c
#define PBLBOOT_MAGIC 0x96f3b83d
struct firmware_header {          // 28 bytes, __packed  (pack "<LLQLLL")
  uint32_t magic;                 // 0x96f3b83d
  uint32_t header_length;         // 28
  uint64_t priority;              // highest valid slot wins; dev band = (0x80<<56)|unixtime
  uint32_t start_offset;          // 0x1000 (firmware body / vector table offset)
  uint32_t length;                // body length
  uint32_t crc;                   // zlib/IEEE CRC-32 over [base+start_offset, +length)
};
```

- CRC is **zlib/IEEE CRC-32** (`crc32_ieee`, identical to Python `zlib.crc32`), NOT the
  STM32 hardware CRC.
- Firmware body / vector table at `slot_base + 0x1000` = `0x12021000`. pblboot sets MSP
  from VT[0] and jumps to VT[1]; **it does not set VTOR** — the Zephyr image sets its
  own VTOR to `0x12021000` (driven by the code-partition offset).
- **No signature.** ftab plays no role in slot validity — validity is entirely the
  in-image header.
- Packaging: reuse `tools/waf/pblboot.py --offset 4096` (run it from outside `tools/waf`
  or a local `gettext.py` shadows stdlib `gettext`). The port's self-contained tool is
  `zephyr-port-apps/fw/tools/prepend_firmware_header.py`; `package_pbz.py` then invokes
  `tools/mkbundle.py` to zip `firmware.bin` + `manifest.json` (type `normal`, hwrev
  `obelix_pvt`, slot 0). The manifest CRC is the STM32/legacy CRC over the whole headered
  `firmware.bin` (what PRF's PutBytes commit verifies) — a *separate* layer from the zlib
  CRC inside the pblboot header.

## 5. Phase-by-phase status

### Phase 1 — Resources — DONE, HW-verified
Pixel-perfect deltas closed (highlight teal `GColorVividCerulean`, "Sounds & Haptics",
launcher icons + status bar). Landed at `646bb62bf` (incl. the UART framebuffer
screenshot tool `zephyr-port-notes/tools/fb_reconstruct.py`).

### Phase 2 — BLE into the unified fw — DONE, HW-verified
Brought NimBLE host + SF32 LCPU controller + `sifli_lrc_glue.c` (the mandatory
`HAL_Delay_us`→`k_busy_wait` override) + `ppog_min.c` + pairing/reversed services +
`ram_store.c` into the fw image, started after PFS on dedicated threads. Replaces the old
`FW_STUB ble_comm`. Commit `84dd76aa6`. Markers `FW_BLE_INIT/ADV/CONTROLLER_UP/PPOG_UP`.
Link: FLASH ~13.6%, RAM ~69.5% (with the malloc arena below).

### Phase 3 — PRF bond reuse — DONE, HW-verified
At BLE init, read the single bond the FreeRTOS PRF wrote to `SHARED_PRF_STORAGE`
(`0x13FFF000`, 256-byte `SharedPRFData`, magic "SPRF", v2) and map `SprfBlePairingData`
into NimBLE `ble_store_value_sec` records (our LTK keyed by `l_ediv/l_rand`, peer LTK,
peer IRK for RPA resolution). Mirrors shipping `bluetooth_persistent_storage_prf.c`.
Files `sprf_bond.c` + `ram_store.c`. Commit `407450e90`. Marker
`FW_BLE_BOND_LOADED peer=24:95:2F:4D:89:F6` observed on hardware.

### Phase 4 — Bootable pbz (relink + packaging) — DONE, HW-verified
Relinked the fw as a slot-0 image (`pt2.overlay`: body at `0x12021000`), added the
header-prepend tool + `package_pbz.py`, extended `mkbundle.py`. Initial version had three
bugs that pblboot rejected ("No valid firmware image"); all fixed and HW-verified in
`a1e40e666`:
1. body offset `0x200` → `0x1000`,
2. inner header CRC STM32 → zlib,
3. dev-band `priority`.
Result: stock pblboot logs `slot0 firmware valid (0x12021000, ...)` → `Loading slot0
firmware @ 0x12021000` → Zephyr boots (`FW_BOOT`, `FW_PFS_MOUNT_OK`, `FW_APP_COUNT 26`).

### Phase 5 — HW test (Pixel 7a + rig) — DONE, HW-verified
- Clean slate (region-wise erase → ftab v1.0.18 → pblboot v0.9.21 → PRF v4.30) ✅.
- pblboot boots Zephyr from slot0 ✅.
- BLE controller + LCPU up ✅; PRF bond loaded from SPRF ✅.
- **Bonded reconnect ✅** — after the GATT fix, CoreApp reconnects with no re-pair:
  `ConnectivityWatcher (read): connected=true paired=true encrypted=true`,
  `FW_BLE_PPOG_UP`, watch shows `v4.0.0-zephyr`.
- **OTA install over BLE ✅** — the full goal. Erased slot0 (→ PRF running), then drove
  CoreApp's "Sideload firmware?" dialog (VIEW intent on the pbz staged in CoreApp's own
  files dir; scoped-storage blocks `/sdcard/Download`). CoreApp PutBytes the Zephyr pbz to
  PRF over BLE: `Firmware update progress → PutBytesCommit (CRC matched) → No resources to
  send, resource PutBytes skipped → PutBytesInstall → Firmware update completed, waiting
  for reboot`. Watch self-rebooted; pblboot logged `slot0 firmware valid (0x12021000,
  0x800000006a8e2968)` and booted Zephyr; the phone reconnected and blob_db notifications
  flowed (`FW_BLE_PP_RX endpoint=0xb1db`). **Firmware-only pbz accepted (risk cleared).**
  A real FreeRTOS→Zephyr over-the-air upgrade, verified end to end.

### Phase 6 — Land + Zephyr self-OTA — OUTSTANDING
- OTA slot-apply infra (`fw_ota_boot.c`) is already on the port base (from the earlier
  rebase at `3881dd890`).
- The BLE **receive** path (PutBytes over PPoGATT → OTA slot) exists build-only on
  `fork/wip/ble-ota-transport` but is **not integrated into the fw image**. Integrating
  it + a reboot-into-slot handoff is the remaining work for Zephyr-to-Zephyr OTA.

## 6. Bugs found and fixed on hardware (this effort)

1. **pblboot rejected the image** — wrong header layout/offset/CRC (see Phase 4). Fixed.
2. **Boot panic in NimBLE** — `ble_gatts_add_svcs` uses libc `realloc`; the fw lacked a
   malloc arena, so it returned NULL and `ble_svc_gap_init` panicked
   (`ble_svc_gap.c:302`). Fixed by `CONFIG_COMMON_LIBC_MALLOC=y` +
   `CONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE=65536` (matching the standalone ble app).
   Commit `a1e40e666`.
3. **Bonded reconnect failed at GATT** — the phone reused the FreeRTOS-PRF ATT handle map
   and read the pairing-service connectivity char (`00000001-328e-0fbb-c642-1aa6699bdada`)
   at a stale handle → `GATT_INVALID_HANDLE` → CoreApp disconnected. Fixed by forcing
   re-discovery: resolve the Service Changed value handle via `ble_gatts_find_chr`, seed an
   indicate-enabled `ble_store_value_cccd` for the imported peer, and call
   `ble_svc_gatt_changed(0x0001,0xffff)` on the bonded peer's `ENC_CHANGE`. Commit
   `ad8390f3c`. (Vendored NimBLE has no GATT Database Hash char, so the automatic
   robust-caching path is unavailable.)

Process lesson: `sftool` can exit 0 on a *missed* program window — always use
`--connect-attempts 0` and read the flash back to verify. Never whole-chip `erase_flash`
(times out); use `erase_region`.

## 7. Branches / commits map (fork)

- `zephyr/obelix` @ `ad8390f3c` — port line, all phases 1–4 + HW fixes (current tip).
- `wip/ota-integ` @ `ad8390f3c` — integration branch (same content).
- `wip/pixel-perfect-2` — Phase 1.
- `wip/ble-in-fw` — Phase 2 standalone.
- `wip/pbz-pack` — Phase 4 standalone.
- `wip/ble-ota-transport` — Phase 6 receive path (build-only, not integrated).
- Base `646bb62bf` — Phase 1 + UART screenshot tool.

Key commits on the port line: `84dd76aa6` (BLE-in-fw), `fd3cc8c5f` (pbz pack),
`407450e90` (bond reuse), `a1e40e666` (pblboot header + malloc), `ad8390f3c` (GATT
re-discovery).

## 8. Rig / test procedure (unicorn-mac-1)

- SSH: `sshpass -p pebblepass ssh -o PubkeyAuthentication=no
  -o PreferredAuthentications=password unicorn-mac-1@100.87.44.126` (transient
  "Permission denied" = sshd rate-limit, back off and retry). scp needs `-O`.
- Flash: `cd obelix-flash && python3 step.py --connect-attempts 0 write_flash <file>`
  (PPK2 power-cycles for the ROM window). Read back with `read_flash /tmp/x@ADDR:SIZE`.
- Console bauds: pblboot 115200; Zephyr fw printk 1000000; ROM banner "SFBL" at 1000000.
  Capture: `python3 boot_capture.py <baud> <secs>` (power-cycles + reads).
- Pixel 7a is on the rig USB. adb at `~/platform-tools/adb`. Drive CoreApp:
  `adb shell am force-stop coredevices.coreapp` +
  `monkey -p coredevices.coreapp -c android.intent.category.LAUNCHER 1`; diagnose via
  `adb logcat` (WatchManager / ConnectivityWatcher / BluetoothGatt) and
  `dumpsys bluetooth_manager`.
- Build: `cd /Users/eric/dev/pblboot-ws && ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb
  GNUARMEMB_TOOLCHAIN_PATH=/Applications/ArmGNUToolchain/14.2.rel1/arm-none-eabi
  .venv/bin/west build -b pt2 <fw-app> -d <build> --pristine`. Worktree `third_party`
  submodules are empty → symlink from `/Users/eric/dev/pebbleos-zephyr`.

## 9. Outstanding work

Immediate (this plan):
- **Phase 5 OTA install**: wipe slot0 back to PRF-running, push the pbz to the Pixel, fire
  the `VIEW` intent, and confirm PRF's OTA writes it to slot0 and pblboot boots Zephyr.
  Watch for: does CoreApp accept a firmware-only pbz (no SysResources object)? does the
  manifest CRC match PRF's PutBytes commit CRC?
- **Phase 6 Zephyr self-OTA**: integrate `wip/ble-ota-transport`'s PutBytes-receive into
  the fw + `fw_ota_boot` slot write + boot-bit + reboot handoff; test Zephyr receiving a
  pbz over its own BLE.

Later / future:
- Firmware signing (currently none; pblboot is CRC-only in this config).
- System resources bank in the pbz (the port embeds fonts as `.inc`; a full OTA may want
  the real `system_resources.pbpack`).
- Watch's own new bonds persisting to flash (Zephyr currently RAM-only for *new* bonds;
  reuse of the PRF bond works, but a fresh Zephyr-side pairing is not yet persisted).
- Harder apps (Timeline/Health/Workout/Send-Text; Notifications), AppDB code bank on the
  now-reliable PFS, P4 build convergence.

## 10. Open questions / risks

- CoreApp firmware-only-pbz acceptance (no resources object) — unverified until the OTA
  install test runs.
- The connectivity read reported `hasRemoteAttemptedToUseStalePairing=true` (with
  `NO_ERROR`) — benign so far, but worth watching once new Zephyr-side bonds are persisted.
- Advertising uses the controller's identity address; matched the phone's bonded address in
  testing, but the watch's own IRK/RPA persistence is not yet handled (only the PRF bond's
  peer IRK is loaded).
