# PebbleOS core firmware bring-up

This application is the P2 unified-firmware bring-up for `pt2`. It compiles the
real firmware entry, Pebble task registry, launcher event loop, kernel event queues, event
service/client dispatch, system background task, NewTimer service, regular
timer, and tick timer service into one Zephyr image.

The `CONFIG_PEBBLE_ZEPHYR_CORE_BOOT` paths retain the production ownership and
queue priorities while limiting the event ABI to tick, callback, and
subscription events. Zephyr supplies the threads, queues, queue sets, mutexes,
semaphores, ticks, and RTC beneath those sources.

Expected UART milestones are:

```text
FW_BOOT
FW_BOOT_SLOT normal|prf
FW_TASK NewTimers up
FW_TASK KernelBackground up
FW_TASK KernelMain up
FW_SERVICES_OK
FW_PFS_MOUNT_OK
FW_PFS_IO_OK <crc>
FW_PFS_UP
FW_REGISTRY_UP
FW_APP_COUNT <count>
FW_APP <uuid-or-id> <name>
FW_BLE_INIT
FW_BLE_CONTROLLER_UP
FW_BLE_ADV
FW_BLE_CONNECTED handle=<handle>
FW_BLE_PAIRED handle=<handle>
FW_BLE_PPOG_UP handle=<handle>
FW_EVENT_LOOP_UP
FW_TICK HH:MM:SS
FW_TIMER dispatched
```

`FW_BOOT_SLOT` comes from a pblboot-compatible slot check: both normal slots
(`0x12020000`, `0x12320000`) are read for a valid pblboot/PebbleOS firmware
header + CRC, the higher-priority valid image wins, and PRF is reported when
neither validates. These regions are read-only from this app.

The OTA receive scaffold (`fw_ota_receive_image()`) validates a complete
firmware blob's CRC, erases and writes the staging region through the real
SiFli QSPI flash driver (via `flash_shim.c` -> `flash_impl_*`), re-validates the
payload from flash, then writes the header last as the bootable commit marker.
A successful call emits:

```text
FW_OTA_RECV
FW_OTA_VALIDATED
FW_OTA_SLOT_SET
```

Staging goes to a dedicated non-bootable OTA scratch region
(`0x13d00000..0x13d40000`), clear of the PFS window, the pblboot slots, and PRF.
pblboot's header format is identical to ours (same magic, priority-selected), so
staging must never land in a real slot until images are executable and signed.
Configure with `-DFW_OTA_TEST_INJECT=ON` to run a local, non-BLE flash-path test
at boot that injects a CRC-valid, deliberately non-executable image through this
path; it is off by default and safe across reboots because the scratch region is
never booted.

PFS mounts the QSPI NOR scratch region at `0x13e00000..0x13e40000` from the
kernel-main task, then creates, writes, reads, verifies, and deletes a self-test
file before entering the launcher event loop. The CRC in `FW_PFS_IO_OK` is
calculated from the readback bytes.

After PFS is ready, the firmware opens PebbleOS's real `appdb` SettingsFile,
enumerates installed `AppDBEntry` records, and combines them with the
default-enabled normal-shell system app list. The minimal launcher selection
prefers the first visible installed app and falls back to TicToc; process loading
is the next slice.

The unified image starts the NimBLE host and SF32 LCPU controller after PFS and
board initialization. It advertises the reversed PPoGATT and pairing services,
uses a RAM-only bond store, and brings up a minimal Pebble Protocol endpoint
router. Persistent PRF bond loading, the full comm-session transport, and BLE
OTA/putbytes remain deferred. The remaining `FW_STUB` boot marker is the display
compositor. Obelix board initialization now binds the real pt2 I2C devices and
performs the shipping legacy-accelerometer reset; mic/audio await Zephyr
drivers. The real SF32 watchdog is fed through Pebble task check-ins, and the
real analytics service records events into its in-RAM backend while the upload
sink awaits a Memfault or native DLS transport.
