# Building your app for the Xteink X4 (RISC-V)

The Xteink X4 is an ESP32-C3, a RISC-V chip. It runs apps built for its own
platform, Folio, as RISC-V code. It does not run the ARM binaries other watches run,
so an app needs a Folio binary to run on it. An app written against the
public SDK needs no change in its source for this: you add the platform and
rebuild.

## Adding Folio

Add `folio` to the platforms in `package.json`:

```json
"targetPlatforms": ["flint", "emery", "gabbro", "folio"]
```

and build as before:

```shell
pebble build
# or, without the pebble tool
export PEBBLE_SDK=/path/to/pebble-sdk
cmake -B build -G Ninja && cmake --build build
```

The .pbw then has a `folio/` directory with the RISC-V binary next to the
ARM binaries of the other platforms. Watches that know nothing of Folio
ignore it. To build Folio alone while you work on it, use
`-DPEBBLE_PLATFORMS=folio`.

The RISC-V binary is built with Espressif's GCC (`riscv32-esp-elf-gcc`, 14.2), which
the build looks for in three places, in this order:

- next to the SDK's ARM toolchain;
- on `PATH`;
- where PlatformIO installs it (`~/.platformio/packages/toolchain-riscv32-esp`).

Configuring a project for Folio without it says so. The ARM platforms
still build with the ARM toolchain, in the same build directory: the build
switches compiler for the Folio targets.

## What is different

The X4 has the Folio platform's display, 240 × 400 black and white, and its
limits are those of the other platforms: 64 KiB for an app's code and heap.

- **Watchfaces** are shown at the classic 144 × 168, framed in black, like
  the X4's own watchfaces. Watchface code written for 144 × 168 works as it
  is.
- **Apps** get the whole 240 × 400. An app that lays itself out from its
  window's bounds fills it. One that places things at fixed coordinates for
  144 × 168 draws them in the top left corner.
- **Code that is ARM:** inline assembly, ARM intrinsics and `__attribute__`s
  specific to ARM do not compile. Prebuilt static libraries, including
  Pebble packages that ship only ARM archives, do not link: a package needs
  a Folio library, built from its source.
- **Floating point** is software, as on the other watches, but RISC-V's
  (libgcc of the RISC-V toolchain).
- **Code size:** RISC-V code is somewhat larger than Thumb: count on 10–30 % more
  binary for the same source.
- **The stack** of an app and of a worker is 4 KiB, which the firmware's
  own functions share when the app calls them. A worker has 2.6 KiB less
  for its code and heap than on a watch.
- **No protection:** an app runs with nothing between it and the system.
  A bug that writes outside the app's memory can crash the watch rather than
  just the app.
- **PebbleKit JS** is unchanged: it runs on the phone.

## Trying it in the emulator

The `qemu_esp32c3` board is the X4's chip in Espressif's QEMU
({doc}`../boards/qemu_esp32c3/index`). It runs Folio binaries as the X4
does, and ARM binaries interpreted by Samsa as well. Run its firmware with
the console on a TCP port, which carries the Pebble protocol:

```shell
pbl configure -b build-c3 --board qemu_esp32c3 && pbl build -b build-c3
pbl run -b build-c3 --console-port 5721
```

and install the .pbw over that port.

The `pebble` tool and libpebble2 still pick a pbw's binary by the hardware
name the watch reports, which for this watch selects `flint/`. Until they pick
by the platform description the watch reports ({doc}`platforms`, "Which
binary a watch runs"), point the installer at `folio/`. With libpebble2:

```python
from libpebble2.util.bundle import PebbleBundle
PebbleBundle.prefixes_for_hardware = classmethod(lambda cls, hw: ("folio/",))
```

## Sideloading to an X4

The X4's console, on its USB port, carries the Pebble protocol too. The same
install works with the serial port in place of the emulator's TCP port.
The X4 has no Samsa: an app whose .pbw has no Folio binary is refused when it is
launched, with "This app was not built for this watch".

## Limitations

- An app crash is not contained, as said above.
- Workers run as apps do, but have only been tried in the emulator.
- The phone app and the app store do not yet offer Folio binaries to the
  X4.
