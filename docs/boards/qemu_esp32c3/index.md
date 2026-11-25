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

The Pebble firmware (`pebble/`) runs on it as Folio, the X4's platform:
240 × 400, twice as large on the 480 × 800 panel, with simulated sensors.
Flint, three times as large, is a change of the platform and of the display
size in `pebble/boards/qemu_esp32c3.conf` and `.overlay`.

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

Quit with Ctrl-A X, or Ctrl-C with a display.

With a display, QEMU's own window would show the panel as its controller
scans it, a quarter turn sideways, at most 800 × 600, and it takes no input.
`pbl run` runs QEMU without it and opens a window of its own instead
(`pbl.qemu_view`): the panel upright, from screendumps of the QEMU monitor,
at half its size, the firmware's own 240 × 400, or at its size with
`python -m pbl.qemu_view <build>/qemu-mon.sock --full`. It redraws only what
changed and remembers where it was put. Its Back, Up, Select and Down
buttons, and the keys of the native simulator (arrows Up and Down; Right,
Return or space for Select; Left, Backspace or Escape for Back), press the
watch's buttons through the shell's `button` command; held past half a
second, they are a long press. The console stays on the terminal.

With `--console-port`, the window sends each press over a connection of its
own to that port, which is free for other clients in between, e.g.
`nc 127.0.0.1 12345` for the shell.

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
chip in `pebble/boards/qemu_esp32c3_normal.conf`. The watch reports itself
as the Flint emulator, so the host tools install a `.pbw`'s Flint binary,
which runs in a 144 × 168 bezel. The integration harness's connection
(`tests/integration/harness/connections.py`, `start_protocol` over
`pblconsole`'s frame transport) carries libpebble2's app installer.

## Bluetooth

The firmware talks to an H4 controller on UART1, the machine's second
`-serial`, when built with `CONFIG_NIMBLE_HCI_UART`, as the Pebble QEMU
boards do on their fourth serial port. Without it the host runs on the fake
controller. `pbl run --bt-hci` gives the controller as a QEMU chardev, and
the integration harness launches the machine itself with Bumble's software
controllers (`virtual`), so the Bluetooth tests run against the RISC-V image:

```shell
pbl configure -b build-hci --board qemu_esp32c3 -DCONFIG_NIMBLE_HCI_UART=y
pbl build -b build-hci
pbl itest -b build-hci bluetooth/
```
