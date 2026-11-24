# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

"""Espressif chips booting straight from their flash, written with esptool
over the chip's USB-Serial-JTAG or UART download port.

Its main() is the console the runner hands out: it flashes the image, resets
the chip, and copies the port to stdout until killed.
"""

import argparse
import os
import shutil
import subprocess
import sys
import time

from .core import Runner, RunnerCaps, RunnerError, UnsupportedOperation

# Where the port comes from when no --tty or lab gives one, as esptool itself
PORT_ENV = "ESPTOOL_PORT"


def _esptool():
    esptool = shutil.which("esptool") or shutil.which("esptool.py")
    if not esptool:
        raise RunnerError("no esptool found, install it (pip install esptool)")
    return esptool


def flash_argv(soc, port, image, reset=True):
    """The image is the ROM's direct boot image, flashed as is at 0: there is
    no image header for esptool to patch the flash settings into."""
    return [
        _esptool(),
        "--chip", soc.lower(),
        "--port", port,
        "--after", "hard-reset" if reset else "no-reset",
        "write-flash",
        "--flash-mode", "keep", "--flash-freq", "keep", "--flash-size", "keep",
        "0x0", image,
    ]  # fmt: skip


class EsptoolRunner(Runner):
    name = "esptool"
    lab_ports = ("serial",)

    @classmethod
    def capabilities(cls):
        return RunnerCaps(commands={"flash", "run"})

    @classmethod
    def supports(cls, cfg):
        return bool(cfg.soc) and cfg.soc.startswith("ESP32")

    @classmethod
    def do_create(cls, cfg, args):
        # The port is locked like one from --tty
        return cls(cfg._replace(tty=cfg.tty or os.getenv(PORT_ENV)))

    def _port(self):
        port = self.cfg.tty
        # Unsupported rather than failed: pbl samples then only builds
        if not port:
            raise UnsupportedOperation(
                f"Port not specified, use --tty, {PORT_ENV} or a lab file"
            )
        return port

    def console_command(self):
        """Flash, then the console from the reset on, on stdout."""
        argv = [
            sys.executable, "-c", "from pbl.runners.esptool import main; main()",
            "--chip", self.cfg.soc, "--flash", self.cfg.bin_file, self._port(),
        ]  # fmt: skip
        return argv, {}

    # Running on the chip is flashing it, as on any device; "run" is there for the console
    # command, which pbl samples takes from a runner that runs.
    def do_run(self, command):
        argv = flash_argv(self.cfg.soc, self._port(), self.cfg.bin_file)
        self.call(self.quote(*argv))


def _monitor(port):
    import serial

    with serial.Serial(port, 115200, timeout=0.1) as p:
        # DTR low with RTS high is the USB-Serial-JTAG's chip reset
        p.dtr = False
        p.rts = True
        time.sleep(0.1)
        p.rts = False
        while True:
            data = p.read(4096)
            if data:
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chip", required=True)
    parser.add_argument("--flash", metavar="IMAGE", help="flash this image first")
    parser.add_argument("port")
    args = parser.parse_args()
    if args.flash:
        subprocess.check_call(flash_argv(args.chip, args.port, args.flash, reset=False))
    try:
        _monitor(args.port)
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
