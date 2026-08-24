# Sandboxed Sliding Text watchface

This standalone Zephyr application loads and relocates the real Emery Sliding
Text PBW, patches its SDK jump table, and starts its entry point on a Zephyr
thread with `CONTROL.nPRIV=1`.

The custom thread-restore hook replaces the broad privileged SRAM MPU region
with two app mappings while that thread runs:

- an RWX arena containing the relocated PBW, app heap, and app-visible time
  result;
- a separate RW/XN app stack.

An explicit user-RO/execute MPU region covers firmware text, constants, the SDK
table, and the syscall island (the pt2 config reports a zero-sized default flash
region). The PBW jump table points at firmware wrappers generated with the local `DEFINE_SYSCALL`; each wrapper
issues `svc #4`. The custom SVC hook accepts only instructions inside the
linker-bounded syscall island, preserves the app return address, elevates the
thread for the privileged implementation, and returns through a trampoline
that restores `CONTROL.nPRIV`.

Tick waiting and framebuffer rendering run in privileged syscall bodies. The
PBW tick and animation callbacks run after returning to unprivileged mode.

Expected UART proof markers are:

    SANDBOX_MPU arena=... stack=... code=... slots=...,...,...
    SANDBOX_APP_UP
    SANDBOX_SYSCALL_OK
    SANDBOX_TICK HH:MM
    SANDBOX_FRAME 0x........

Set `-DWATCHFACE_ASCII_PREVIEW=ON` to print the framebuffer preview after each
CRC. Deliberate MPU fault injection is not enabled in this image because it
would terminate the watchface thread; the separate sandbox spike remains the
hardware proof for fault containment.

Build from the Zephyr workspace:

    ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb \
    GNUARMEMB_TOOLCHAIN_PATH=$(dirname $(dirname $(which arm-none-eabi-gcc))) \
    .venv/bin/west build -b pt2 \
      /Users/eric/dev/pebbleos-zephyr/zephyr-port-apps/watchface_sandboxed \
      -d /Users/eric/dev/pebbleos-zephyr/build-wfsb
