# SPI

An SPI bus carries full-duplex transfers to peripherals, with the chip select
asserted throughout.

| | |
| --- | --- |
| Directory | `drivers/spi` |
| Header | `pbl/drivers/spi.h` |
| Kconfig | `CONFIG_SPI`, selected by the drivers and code that use it |
| API | {apidoc}`API reference <drivers_spi>` |

## Drivers

| Driver | Compatible | Kconfig |
| --- | --- | --- |
| Espressif ESP32-C3 SPI Controller | `esp,esp32c3-spi` | `CONFIG_SPI_ESP32C3` |
| Nordic nRF5 SPI Controller | `nordic,nrf52832-spim` | `CONFIG_SPI_NRF5` |
