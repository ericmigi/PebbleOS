# QEMU ESP32-C3 (qemu_esp32c3)

## Overview

:::{div} board-card

:Board: `qemu_esp32c3`
:SoC: Espressif ESP32-C3
:::

The `esp32c3` machine of [Espressif's QEMU](https://github.com/espressif/qemu),
which emulates the ESP32-C3 and its boot ROM. It runs the kernel and its
samples, with UART0 for the console and the machine's virtual framebuffer as an
800 × 480 display, which stands in for the [](../xteink_x4/index.md) panel;
there is no input device or Bluetooth.

The firmware is the image the ROM boots straight from the flash: the code
runs in place at 0x42000000 and the read-only data is read through
0x3c000000, as on a real ESP32-C3.

## Toolchain and emulator

The board builds with Espressif's `riscv32-esp-elf` toolchain, found on
`PATH`, where PlatformIO or ESP-IDF install it, or through `CROSS_COMPILE`,
e.g. `CROSS_COMPILE=~/.platformio/packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-`.
Picolibc is built from source.

`pbl run` pads the image into a flash image and boots it on the
`qemu-system-riscv32` of `PEBBLE_QEMU_ESP32_BIN`, or of `PATH`:

```shell
pbl configure --board qemu_esp32c3 samples/hello_world
pbl build
pbl run
```

Quit with Ctrl-A X.
