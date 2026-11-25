# Xteink X4 (xteink_x4)

## Overview

:::{div} board-card

:Board: `xteink_x4`
:SoC: Espressif ESP32-C3
:::

The Xteink X4 is a pocket e-reader built on the ESP32-C3. PebbleOS runs the
kernel and its samples on it, with the console on the chip's USB port, the
e-paper display, the keys (two ADC ladders, polled, and the power button) and
the battery voltage (ADC channel 0, half the cell voltage); the microSD card is
not supported yet.

The Pebble firmware (`pebble/`, the default application) runs as folio: a
240 × 400 screen twice as large, filling the panel, with watchfaces at
their classic 144 × 168 centred on black; third-party apps are the X4's own
RISC-V code, built for Folio ({doc}`../../sdk/xteink_x4`), and an app with
only ARM binaries is refused, as the board has no Samsa; the system
resources and the filesystem in the flash, the keys as Back, Up, Select and
Down. Sensors, vibration, the microphone and the charger are simulated.
Bluetooth LE runs on the chip's own radio, see [Bluetooth](#bluetooth).

The firmware is the image the ROM boots straight from the flash, as on
[](../qemu_esp32c3/index.md): no second-stage bootloader, the code running in
place at 0x42000000. The early init holds the power latch, sets the CPU to
160 MHz on the PLL the ROM leaves running, and turns the watchdogs off.

## Hardware

- **SoC**: ESP32-C3 (QFN32), a 160 MHz RV32IMC core with 400 KiB of SRAM,
  and a 40 MHz crystal
- **Flash**: 16 MiB SPI NOR in DIO mode, of which the ROM maps the first
  4 MiB for the firmware
- **Display**: 800 × 480 e-paper panel (SSD1677) on SPI at 40 MHz: SCLK GPIO8,
  MOSI GPIO10, CS GPIO21, DC GPIO4, RST GPIO5, BUSY GPIO6. A fast refresh takes
  about 200 ms with the board's shortened waveform (`fast-frames`, 416 ms with
  the panel's own), every 20th refresh is a 1.7 s clean one
- **Storage**: microSD card slot on the same bus, MISO GPIO7 and CS GPIO12
- **Keys**: two ADC ladders, on GPIO1 and GPIO2, and a power button on GPIO3
- **Power**: battery on GPIO0 through a halving divider; GPIO13 latches the
  battery on, GPIO20 reads high with USB connected
- **USB**: USB-C to the chip's USB-Serial-JTAG, for the console and flashing

UART0's pins are the panel's chip select and the USB detect input, so the
console is the USB-Serial-JTAG port. The ROM still prints its banner on
GPIO21 before the firmware starts; the firmware then drives the pin as the
panel's chip select. GPIO12 and GPIO13, the flash's in quad mode, are free with
the flash in DIO mode: GPIO12 is the microSD card's chip select, held high, and
GPIO13 the power latch, held high.

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

The Pebble firmware also needs its system resources, which `--resources`
writes in the same esptool run, at the `system-resources-bank-0` partition:

```shell
pbl configure --board xteink_x4
pbl build
pbl run --resources --tty /dev/cu.usbmodem1101
```

Its first boot formats the filesystem partition, which takes minutes when the
flash holds the reader's own data.

{doc}`../../pbl/samples` checks samples on the reader the way it does on the
emulator, flashing each in turn and reading the console from the reset on:

```shell
ESPTOOL_PORT=/dev/cu.usbmodem1101 pbl samples --board xteink_x4 samples/kernel
```

Flashing replaces the reader's own bootloader and firmware. Back the flash up
first, with `esptool read-flash 0 0x1000000 backup.bin`, to write it back
with `esptool write-flash 0 backup.bin`.

## Bluetooth

The Pebble firmware pairs with the phone app and keeps its connection over
the ESP32-C3's radio: the NimBLE host on Espressif's Bluetooth LE controller,
reached through its virtual HCI (`third_party/nimble/transport/hci_esp32c3.c`).
The controller and its PHY are **closed libraries** from Espressif,
`libbtdm_app.a`, `libphy.a` and `libbtbb.a`, in the `esp32c3-bt-lib` and
`esp-phy-lib` submodules at the versions ESP-IDF 5.5.5 pins, with the glue
that runs them on PebbleOS in `third_party/espressif_bt`. Fetch them before
building:

```shell
git submodule update --init --depth 1 -- third_party/espressif_bt/esp32c3-bt-lib third_party/espressif_bt/esp-phy-lib
```

The controller comes up at every boot (`CONFIG_ESP32C3_BT_ARMED_BY_DEFAULT`).
Each bring-up runs under the RTC watchdog: one that hangs resets the chip,
and a bring-up that failed or hung keeps the radio off at the following
boots, the firmware then running on a fake controller and logging so. `btc
status` shows the state, `btc clear` and a restart retry. Airplane mode
stops the host, so nothing is on air, and starts it again.

Its interrupt handler runs from the flash, which is why the board runs the
flash at 80 MHz: at the boot's 20 MHz the controller misses its timing and
stops within seconds. The controller takes about 23 KiB of heap and as much
RAM code; firmware updates from the phone are refused, as nothing could
install them (`CONFIG_FIRMWARE_UPDATE`), and the serial number is the chip's
MAC address.

To build without Bluetooth, leaving the radio off and the libraries out:

```shell
pbl configure --board xteink_x4 -DCONFIG_ESP32C3_BT=n
```
