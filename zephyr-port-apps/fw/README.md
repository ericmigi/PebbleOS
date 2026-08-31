# PebbleOS core firmware bring-up

This application is the P2 unified-firmware bring-up for `pt2`, also buildable
for the `qemu_emery` board (qemu-pebble `pebble-emery` machine): the SiFli
HAL/QSPI/NimBLE stack is pt2-only (see `if(FW_BOARD_PT2)` in CMakeLists.txt and
`boards/pt2.conf`); qemu uses the Zephyr flash/input/rtc drivers, the FreeRTOS
`qemu_image_spi` flash layout, and stubs BLE (`FW_BLE_SKIPPED`) and the
watchdog. It compiles the
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
neither validates. The running slot0 remains read-only; BLE self-OTA targets
slot1.

The minimal PPoGATT router accepts firmware updates from CoreApp on the system
message (`0x0012`) and PutBytes (`0xBEEF`) endpoints. It erases the received
image span in slot1 (`0x12320000..0x12620000`), streams the headered
`firmware.bin` there verbatim through the real SiFli QSPI driver, verifies both
the PutBytes legacy checksum and the pblboot in-header IEEE CRC, and writes the
28-byte header last on Install. A successful transfer emits:

```text
FW_OTA_RECV_BEGIN slot=0x12320000 size=<bytes> append=<bytes>
FW_OTA_PUT <written>/<total>
FW_OTA_COMMIT calc=<crc> expected=<crc> MATCH
FW_OTA_SLOT1_WRITTEN base=0x12320000 size=<bytes>
FW_OTA_INSTALL_REBOOT
```

Configure with `-DFW_OTA_TEST_INJECT=ON` to run a local, non-BLE flash-path test
at boot. The deliberately non-executable test image still uses the dedicated
non-bootable scratch region (`0x13d00000..0x13d40000`); it is off by default.

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
router with firmware PutBytes receive support. The router remains a
single-connection implementation without the production comm-session transport
or outbound retransmit window. The remaining `FW_STUB` boot marker is the
display compositor. Obelix board initialization now binds the real pt2 I2C
devices and performs the shipping legacy-accelerometer reset; mic/audio await
Zephyr drivers. The real SF32 watchdog is fed through Pebble task check-ins,
and the real analytics service records events into its in-RAM backend while the
upload sink awaits a Memfault or native DLS transport.
