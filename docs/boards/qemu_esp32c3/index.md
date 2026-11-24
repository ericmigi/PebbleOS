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

The Pebble firmware (`pebble/`) runs on it as Flint, three times as large on
the display, with simulated sensors; the shell's `button click` command
presses its buttons.

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

The flash image, `qemu_esp32_flash.bin` in the build directory, is sized to
the partitions, with the system resources at their partition when the build
has them. The rest of an existing image is kept, so the filesystem survives
between runs, as on a watch; delete the file for a factory-fresh flash.

## Host tools and third-party apps

The console carries the Pebble protocol in frames between its log lines, as
a watch's does. `--console-port` serves the console on a TCP port instead of
the terminal, for the host tools that speak it, e.g. the integration tests:

```shell
pbl run --console-port 12345
pbl itest --connection serial:socket://127.0.0.1:12345
```

The Pebble firmware's normal variant runs third-party apps from `.pbw`
files, installed over that connection as a phone installs them: Samsa
interprets their ARM code, with what it keeps per process sized for the
chip in `pebble/boards/qemu_esp32c3_normal.conf`.
