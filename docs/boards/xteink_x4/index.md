# Xteink X4 (xteink_x4)

## Overview

:::{div} board-card

:Board: `xteink_x4`
:SoC: Espressif ESP32-C3
:::

The Xteink X4 is a pocket e-reader built on the ESP32-C3. PebbleOS runs the
kernel and its samples on it, with the console on the chip's USB port; the
e-paper display, the keys and the microSD card are not supported yet.

The firmware is the image the ROM boots straight from the flash, as on
[](../qemu_esp32c3/index.md): no second-stage bootloader, the code running in
place at 0x42000000. The early init holds the power latch, sets the CPU to
160 MHz on the PLL the ROM leaves running, and turns the watchdogs off.

## Hardware

- **SoC**: ESP32-C3 (QFN32), a 160 MHz RV32IMC core with 400 KiB of SRAM,
  and a 40 MHz crystal
- **Flash**: 16 MiB SPI NOR in DIO mode, of which the ROM maps the first
  4 MiB for the firmware
- **Display**: 800 × 480 e-paper panel (SSD1677) on SPI: SCLK GPIO8, MOSI
  GPIO10, CS GPIO21, DC GPIO4, RST GPIO5, BUSY GPIO6
- **Storage**: microSD card slot on the same bus, MISO GPIO7 and CS GPIO12
- **Keys**: two ADC ladders, on GPIO1 and GPIO2, and a power button on GPIO3
- **Power**: battery on GPIO0 through a halving divider; GPIO13 latches the
  battery on, GPIO20 reads high with USB connected
- **USB**: USB-C to the chip's USB-Serial-JTAG, for the console and flashing

UART0's pins are the panel's chip select and the USB detect input, so the
console is the USB-Serial-JTAG port. The ROM still prints its banner on
GPIO21 before the firmware starts. GPIO12 and GPIO13, the flash's in quad
mode, are free with the flash in DIO mode: GPIO13 is the power latch, held
high.

## Flashing

The `esptool` runner writes the image at the start of the flash with
[esptool](https://github.com/espressif/esptool), leaving the flash settings as
they are, over the USB port given with `--tty`, `ESPTOOL_PORT` or a
{doc}`lab file <../../pbl/lab>`. esptool resets the chip into its download
mode through the port, whatever the firmware does:

```shell
pbl configure --board xteink_x4 samples/hello_world
pbl build
pbl run --tty /dev/cu.usbmodem1101
pbl console --tty /dev/cu.usbmodem1101
```

{doc}`../../pbl/samples` checks samples on the reader the way it does on the
emulator, flashing each in turn and reading the console from the reset on:

```shell
ESPTOOL_PORT=/dev/cu.usbmodem1101 pbl samples --board xteink_x4 samples/kernel
```

Flashing replaces the reader's own bootloader and firmware. Back the flash up
first, with `esptool read-flash 0 0x1000000 backup.bin`, to write it back
with `esptool write-flash 0 backup.bin`.
