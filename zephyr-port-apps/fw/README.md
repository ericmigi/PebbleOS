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
FW_EVENT_LOOP_UP
FW_TICK HH:MM:SS
FW_TIMER dispatched
```

The boot marker comes from the pblboot-compatible dual-slot selector: both
normal slots are checked for a valid pblboot header and CRC, the valid image
with the higher 64-bit priority is selected, and PRF is selected when neither
normal slot validates.

The OTA receive scaffold accepts a complete pblboot-format image through
`fw_ota_receive_image()`. It validates the in-memory CRC, erases and writes the
inactive normal slot (or the PRF region for a recovery image), validates the
payload from flash, and writes the header last as the bootable commit. A
successful call emits:

```text
FW_OTA_RECV
FW_OTA_VALIDATED
FW_OTA_SLOT_SET
```

For a local, non-BLE flash-path test, configure with
`-DFW_OTA_TEST_INJECT=ON`. The injected payload is intentionally not executable;
do not reboot into that slot. This option is off by default.

PFS mounts the QSPI NOR scratch region at `0x13e00000..0x13e40000` from the
kernel-main task, then creates, writes, reads, verifies, and deletes a self-test
file before entering the launcher event loop. The CRC in `FW_PFS_IO_OK` is
calculated from the readback bytes.

After PFS is ready, the firmware opens PebbleOS's real `appdb` SettingsFile,
enumerates installed `AppDBEntry` records, and combines them with the
default-enabled normal-shell system app list. The minimal launcher selection
prefers the first visible installed app and falls back to TicToc; process loading
is the next slice.

The boot also prints one `FW_STUB` line for each remaining intentionally
deferred service family: display/compositor and BLE/communications. Obelix
board initialization now binds the real pt2 I2C devices and performs the
shipping legacy-accelerometer reset; mic/audio await Zephyr drivers. The real
SF32 watchdog is fed through Pebble task check-ins, and the real analytics
service records events into its in-RAM backend while the upload sink awaits a
Memfault or native DLS transport.
