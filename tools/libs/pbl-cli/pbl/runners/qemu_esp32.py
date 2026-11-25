# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

import os
import selectors
import shutil
import signal
import socket
import subprocess
import sys
import time

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

    def __init__(self, cfg, console_port=None, bt_hci=None):
        super().__init__(cfg)
        self.console_port = console_port
        self.bt_hci = bt_hci

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
        # --bt-hci is the qemu runner's, shared with this one: UART1's chardev
        return cls(
            cfg,
            console_port=getattr(args, "console_port", None),
            bt_hci=getattr(args, "bt_hci", None),
        )

    def do_run(self, command):
        build = self.cfg.build
        hci_uart = bool(build.config.CONFIG_NIMBLE_HCI_UART)
        if hci_uart and not self.bt_hci:
            raise RunnerError(
                "the build uses CONFIG_NIMBLE_HCI_UART; pass --bt-hci with the "
                "controller's chardev, e.g. tcp:127.0.0.1:PORT"
            )
        if self.bt_hci and not hci_uart:
            raise RunnerError("--bt-hci needs a build with CONFIG_NIMBLE_HCI_UART=y")
        if build.config.CONFIG_DISPLAY:
            return self._run_with_view(build)
        argv = self._command_line(build, console_port=self.console_port)
        return self.run_program(argv + list(self.cfg.extra_args), cwd=build.topdir)

    def _run_with_view(self, build):
        """QEMU headless, and a window of its own onto the display and the
        buttons (pbl.qemu_view): QEMU's window is sideways for a panel mounted
        a quarter turn, at most 800x600, and has no input. Without a console
        port the console stays on the terminal, through this process, which
        also passes on the window's button presses."""
        port = self.console_port or _free_port()
        argv = self._command_line(build, window=True, console_port=port)
        argv += list(self.cfg.extra_args)
        view = [
            sys.executable, "-m", "pbl.qemu_view", build.join(emulator.MONITOR_SOCKET),
            "--title", f"PebbleOS on QEMU ({build.board})",
        ]  # fmt: skip
        if self.console_port:
            view += ["--port", str(port)]
        if self.cfg.dry_run:
            print("[dry-run] " + self.quote(*argv))
            print("[dry-run] " + self.quote(*view))
            return 0
        print(self.quote(*argv), flush=True)
        # Stopped from outside, the window and QEMU stop with it
        signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
        qemu = subprocess.Popen([str(a) for a in argv], cwd=build.topdir)
        pbl_cli = os.path.dirname(os.path.dirname(os.path.dirname(__file__)))
        env = dict(
            os.environ,
            PYTHONPATH=os.pathsep.join(
                filter(None, [pbl_cli, os.getenv("PYTHONPATH")])
            ),
        )
        viewer = subprocess.Popen(
            [str(a) for a in view],
            env=env,
            stdout=None if self.console_port else subprocess.PIPE,
        )
        try:
            if self.console_port:
                return qemu.wait()
            return _bridge_console(qemu, viewer, port)
        except KeyboardInterrupt:
            return 0
        finally:
            for proc in (viewer, qemu):
                if proc.poll() is None:
                    proc.terminate()
                    proc.wait()

    def console_command(self):
        return self._command_line(self.cfg.build), {}

    @staticmethod
    def _qemu(machine):
        binary = (
            "qemu-system-xtensa"
            if machine in ("esp32", "esp32s3")
            else "qemu-system-riscv32"
        )
        env_var = (
            "PEBBLE_QEMU_ESP32S3_BIN"
            if machine == "esp32s3"
            else "PEBBLE_QEMU_ESP32_BIN"
        )
        qemu = os.getenv(env_var) or os.getenv("PEBBLE_QEMU_ESP32_BIN")
        if (
            qemu
            and os.path.isfile(qemu)
            and os.access(qemu, os.X_OK)
            and (env_var == "PEBBLE_QEMU_ESP32_BIN" or os.path.basename(qemu) == binary)
        ):
            return qemu
        qemu = shutil.which(binary)
        if not qemu:
            raise RunnerError(
                f"no Espressif QEMU: set {env_var} to its {binary}, or put it on PATH"
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
            # The monitor socket takes the screendumps the window shows
            monitor = build.join(emulator.MONITOR_SOCKET)
            display = ["-display", "none", "-monitor", f"unix:{monitor},server,nowait"]
        else:
            display = ["-display", "none", "-monitor", "none"]
        if console_port:
            serial = f"tcp:127.0.0.1:{console_port},server=on,wait=off,nodelay=on"
        else:
            # A plain stdio backend gets nothing through on this machine
            serial = "mon:stdio"
        # Instruction counting keeps single-core RISC-V timers in step with code,
        # but makes dual-core Xtensa (esp32s3) ~80x slower; esp32s3 runs natively.
        icount = ["-icount", "3"] if machine == "esp32c3" else []
        return [
            self._qemu(machine),
            "-machine", machine,
            *display,
            "-serial", serial,
            # UART1: the Bluetooth controller of CONFIG_NIMBLE_HCI_UART
            *(["-serial", self.bt_hci] if self.bt_hci else []),
            "-no-reboot",
            *icount,
            "-drive", f"file={self._flash_image(build)},if=mtd,format=raw",
        ]  # fmt: skip


def _free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def _bridge_console(qemu, viewer, port):
    """The console on the terminal, plus the window's button commands, over
    the console's TCP port, until QEMU ends."""
    for _ in range(100):
        try:
            console = socket.create_connection(("127.0.0.1", port))
            break
        except OSError:
            if qemu.poll() is not None:
                return qemu.returncode
            time.sleep(0.1)
    else:
        raise RunnerError(f"QEMU does not serve the console on port {port}")
    sel = selectors.DefaultSelector()
    sel.register(console, selectors.EVENT_READ, "console")
    sel.register(sys.stdin, selectors.EVENT_READ, "terminal")
    sel.register(viewer.stdout, selectors.EVENT_READ, "window")
    with console:
        while qemu.poll() is None:
            for key, _ in sel.select(timeout=0.5):
                if key.data == "console":
                    data = console.recv(65536)
                    if not data:
                        return qemu.wait()
                    sys.stdout.buffer.write(data)
                    sys.stdout.flush()
                    continue
                line = key.fileobj.readline()
                if not line:
                    sel.unregister(key.fileobj)
                    continue
                console.sendall(line.rstrip("\r\n").encode() + b"\r")
    return qemu.returncode
