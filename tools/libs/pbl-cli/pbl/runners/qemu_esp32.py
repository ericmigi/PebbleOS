# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

import os
import shutil

from .. import emulator, flash_layout
from .core import Runner, RunnerCaps, RunnerError

# The flash image the ROM boots from, the firmware padded to a size the
# emulated flash takes.
FLASH_IMAGE = "qemu_esp32_flash.bin"
FLASH_SIZES = (2 << 20, 4 << 20, 8 << 20, 16 << 20)


class QemuEsp32Runner(Runner):
    """Espressif's QEMU, booting the firmware's flash image through the
    emulated ROM, as the ROM boots it from the flash of a real chip."""

    name = "qemu-esp32"

    def __init__(self, cfg, console_port=None):
        super().__init__(cfg)
        self.console_port = console_port

    @classmethod
    def capabilities(cls):
        return RunnerCaps(commands={"run"})

    @classmethod
    def do_add_parser(cls, parser):
        parser.add_argument(
            "--console-port",
            type=int,
            metavar="PORT",
            help="Serve the console on this TCP port instead of the terminal, for "
            "host tools speaking the Pebble protocol in console frames",
        )

    @classmethod
    def do_create(cls, cfg, args):
        return cls(cfg, console_port=getattr(args, "console_port", None))

    def do_run(self, command):
        build = self.cfg.build
        # A window for a display, as the Pebble QEMU runner opens one
        argv = self._command_line(
            build,
            window=bool(build.config.CONFIG_DISPLAY),
            console_port=self.console_port,
        )
        return self.run_program(argv + list(self.cfg.extra_args), cwd=build.topdir)

    def console_command(self):
        return self._command_line(self.cfg.build), {}

    @staticmethod
    def _qemu():
        qemu = os.getenv("PEBBLE_QEMU_ESP32_BIN")
        if qemu and os.path.isfile(qemu) and os.access(qemu, os.X_OK):
            return qemu
        qemu = shutil.which("qemu-system-riscv32")
        if not qemu:
            raise RunnerError(
                "no Espressif QEMU: set PEBBLE_QEMU_ESP32_BIN to its "
                "qemu-system-riscv32, or put it on PATH"
            )
        return qemu

    def _flash_image(self, build):
        """Write the firmware, and the system resources at their partition,
        into the flash image, keeping the rest of an image already there:
        the filesystem survives a restart, as on a watch."""
        image = build.join(FLASH_IMAGE)
        if self.cfg.dry_run:
            return image
        firmware = build.bin
        if not os.path.isfile(firmware):
            raise RunnerError(f"no {firmware}; run 'pbl build' first")
        contents = [(0, firmware)]
        if os.path.isfile(build.pbpack):
            contents.append(
                (build.flash_region("SYSTEM_RESOURCES_BANK_0")[0], build.pbpack)
            )
        end = max(off + size for off, size in flash_layout.regions(build).values())
        end = max([end] + [off + os.path.getsize(path) for off, path in contents])
        padded = next((s for s in FLASH_SIZES if s >= end), None)
        if padded is None:
            raise RunnerError(f"{firmware} is larger than the largest emulated flash")
        if not os.path.isfile(image) or os.path.getsize(image) != padded:
            with open(image, "wb") as dst:
                dst.write(b"\xff" * padded)
        with open(image, "r+b") as dst:
            for offset, path in contents:
                dst.seek(offset)
                with open(path, "rb") as src:
                    dst.write(src.read())
        return image

    def _command_line(self, build, window=False, console_port=None):
        machine = build.board_spec.qemu.get("machine")
        if not machine:
            raise RunnerError(f"board {build.board} declares no QEMU machine")
        if window:
            # The monitor socket takes screendumps
            monitor = build.join(emulator.MONITOR_SOCKET)
            display = ["-display", "sdl", "-monitor", f"unix:{monitor},server,nowait"]
        else:
            display = ["-display", "none", "-monitor", "none"]
        if console_port:
            serial = f"tcp:127.0.0.1:{console_port},server=on,wait=off,nodelay=on"
        else:
            # A plain stdio backend gets nothing through on this machine
            serial = "mon:stdio"
        return [
            self._qemu(),
            "-machine", machine,
            *display,
            "-serial", serial,
            "-no-reboot",
            # Instruction counting keeps the timers in step with the code
            "-icount", "3",
            "-drive", f"file={self._flash_image(build)},if=mtd,format=raw",
        ]  # fmt: skip
