# Flash

A flash is addressed by offsets from its first byte. Writes only clear bits,
so a range is erased first; the class erases with as few sector and
subsector erases as it can, skipping blank ones. `drivers/flash/README.md`
describes the class core, its coredump mode, which relies on no OS service,
and how NOR parts are described.

| | |
| --- | --- |
| Directory | `drivers/flash` |
| Header | `pbl/drivers/flash.h` |
| Kconfig | `CONFIG_FLASH`, enabled by the application |
| Role | `FLASH` |
| Sample | {doc}`../samples/drivers/flash` |
| API | {apidoc}`API reference <drivers_flash>` |

## Drivers

| Driver | Compatible | Kconfig |
| --- | --- | --- |
| SPI NOR | `jedec,spi-nor` | `CONFIG_FLASH_SPI_NOR` |
| QEMU External Flash | `qemu,pebble-extflash` | `CONFIG_FLASH_QEMU` |
| ESP32-C3 boot flash, through the ROM's routines | `esp,esp32c3-flash` | `CONFIG_FLASH_ESP32C3` |
| POSIX External Flash | — | `CONFIG_FLASH_POSIX` |
