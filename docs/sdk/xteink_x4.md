# Building your app for the Xteink X4 (RISC-V)

The Xteink X4 is an ESP32-C3, a RISC-V chip. It runs apps built for its own
platform, xteinkx4, as RISC-V code. It does not run the ARM binaries other watches run,
so an app needs an xteinkx4 binary to run on it. An app written against the
public SDK needs no change in its source for this: you add the platform and
rebuild.

## Adding xteinkx4

Add `xteinkx4` to the platforms in `package.json`:

```json
"targetPlatforms": ["flint", "emery", "gabbro", "xteinkx4"]
```

and build as before:

```shell
pebble build
# or, without the pebble tool
export PEBBLE_SDK=/path/to/pebble-sdk
cmake -B build -G Ninja && cmake --build build
```

The .pbw then has a `xteinkx4/` directory with the RISC-V binary next to the
ARM binaries of the other platforms. Watches that know nothing of xteinkx4
ignore it. To build xteinkx4 alone while you work on it, use
`-DPEBBLE_PLATFORMS=xteinkx4`.

The RISC-V binary is built with Espressif's GCC (`riscv32-esp-elf-gcc`, 14.2), which
the build looks for in three places, in this order:

- next to the SDK's ARM toolchain;
- on `PATH`;
- where PlatformIO installs it (`~/.platformio/packages/toolchain-riscv32-esp`).

Configuring a project for xteinkx4 without it says so. The ARM platforms
still build with the ARM toolchain, in the same build directory: the build
switches compiler for the xteinkx4 targets.

## What is different

The X4 has the xteinkx4 platform's display, 240 × 400 black and white.

- **Watchfaces** built for xteinkx4 get the whole 240 × 400, as apps do.
  The X4's own watchfaces, and those of classic watches, are shown at
  144 × 168, framed in black.
  One that places things at fixed coordinates for 144 × 168, rather than
  from its window's bounds, draws them in the top left corner: such a
  watchface needs its layout taken from the bounds to build for xteinkx4.
- **Code that is ARM:** inline assembly, ARM intrinsics and `__attribute__`s
  specific to ARM do not compile. Prebuilt static libraries, including
  Pebble packages that ship only ARM archives, do not link: a package needs
  an xteinkx4 library, built from its source (not tried yet).
- **Floating point** is software, as on the other watches, but RISC-V's
  (libgcc of the RISC-V toolchain).
- **Code size:** RISC-V code is somewhat larger than Thumb: count on 10–30 % more
  binary for the same source.
- **Memory:** the stack of an app and of a worker is 4 KiB, which the
  firmware's own functions share when the app calls them. That leaves an
  app 62 KiB for its code and heap (63,488 bytes) and a worker 7,552 bytes,
  less than on a watch, which the build checks.
- **No protection:** an app runs with nothing between it and the system.
  A bug that writes outside the app's memory can corrupt the system, and a
  fault in an app stops the whole watch rather than just the app.
- **PebbleKit JS** is unchanged: it runs on the phone.

## Trying it in the emulator

The `qemu_esp32c3` board is the X4's chip in Espressif's QEMU
({doc}`../boards/qemu_esp32c3/index`). It runs xteinkx4 binaries as the X4
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
binary a watch runs"), point the installer at `xteinkx4/`. With libpebble2:

```python
from libpebble2.util.bundle import PebbleBundle
PebbleBundle.prefixes_for_hardware = classmethod(lambda cls, hw: ("xteinkx4/",))
```

## Sideloading to an X4

The X4's console, on its USB port, carries the Pebble protocol too. The same
install works with the serial port in place of the emulator's TCP port.
The X4 has no Samsa: an app whose .pbw has no xteinkx4 binary is refused when it is
launched, with "This app was not built for this watch".

## Limitations

- An app crash is not contained, as said above.
- xteinkx4 is only built for when `targetPlatforms` names it, and only by the
  CMake build: the deprecated waf build leaves it out.
- Workers run as apps do, but have only been tried in the emulator.
- The phone app and the app store do not yet offer xteinkx4 binaries to the
  X4.
