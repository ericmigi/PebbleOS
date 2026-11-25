# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

import os
import shutil
import subprocess
import tempfile
import time

from harness.device import DeviceAdapter
from harness.device.qemu_adapter import HCI_DRAIN_S, _free_port, _Monitor
from harness.errors import HarnessError

FLASH_IMAGE = "qemu_esp32_flash.bin"


class QemuEsp32Adapter(DeviceAdapter):
    """The firmware under Espressif's QEMU, headless, booted by the emulated ROM from a fresh
    flash image: the console on UART0, the Bluetooth HCI UART on UART1."""

    type = "qemu"

    def __init__(self, config):
        super().__init__(config)
        self.workdir = config.results_dir
        self.console_port = _free_port()
        self._sockdir = None
        self._process = None
        self._log = None
        self._bt_hci = None

    @property
    def monitor_socket(self):
        return os.path.join(self._sockdir, "mon.sock")

    def _flash_image(self):
        """The image pbl run would boot, written afresh in the work directory."""
        from pbl.build import BuildDir
        from pbl.runners.qemu_esp32 import QemuEsp32Runner

        class _Cfg:
            dry_run = False

        cfg = _Cfg()
        cfg.build = BuildDir(self.build.path, self.build.topdir)
        self.machine = cfg.build.board_spec.qemu["machine"]
        runner = QemuEsp32Runner.__new__(QemuEsp32Runner)
        runner.cfg = cfg
        image = os.path.join(self.workdir, FLASH_IMAGE)
        if os.path.exists(image):
            os.unlink(image)
        return runner._flash_image(cfg.build, image=image)

    def _start(self):
        from pbl.runners.qemu_esp32 import QemuEsp32Runner

        command = [
            QemuEsp32Runner._qemu(self.machine),
            "-machine", self.machine,
            "-display", "none",
            "-monitor", f"unix:{self.monitor_socket},server=on,wait=off",
            "-serial", f"tcp:127.0.0.1:{self.console_port},server=on,wait=off,nodelay=on",
            *(["-serial", self._bt_hci] if self._bt_hci else []),
            *(["-icount", "3"] if self.machine == "esp32c3" else []),
            "-drive", f"file={self._flash},if=mtd,format=raw",
        ]  # fmt: skip
        self._log.write(" ".join(command) + "\n")
        self._log.flush()
        self._process = subprocess.Popen(
            command,
            stdin=subprocess.DEVNULL,
            stdout=self._log,
            stderr=subprocess.STDOUT,
        )
        deadline = time.monotonic() + 10
        while True:
            if self._process.poll() is not None:
                raise HarnessError(
                    f"QEMU exited with {self._process.returncode}; see "
                    f"{os.path.join(self.workdir, 'qemu.log')}"
                )
            try:
                # Opening the single-client UART here can reset the actual test connection.
                if "serial0:" in self._monitor("info chardev"):
                    return
            except OSError:
                pass
            if time.monotonic() > deadline:
                raise HarnessError("QEMU did not open its console port")
            time.sleep(0.05)

    def _device_launch(self):
        self._sockdir = tempfile.mkdtemp(prefix="pbl-qemu-")
        os.makedirs(self.workdir, exist_ok=True)
        controller = self._start_bluetooth()
        self._bt_hci = (
            f"tcp:127.0.0.1:{controller}" if isinstance(controller, int) else controller
        )
        self._flash = self._flash_image()
        self._log = open(os.path.join(self.workdir, "qemu.log"), "a")  # noqa: SIM115
        self._start()

    def _close_device(self):
        if self._process is not None and self._process.poll() is None:
            self._process.terminate()
            try:
                self._process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self._process.kill()
                self._process.wait()
        self._process = None
        if self._sockdir is not None:
            shutil.rmtree(self._sockdir, ignore_errors=True)
            self._sockdir = None
        if self._log is not None:
            self._log.close()
            self._log = None
        self._stop_bluetooth()

    def _monitor(self, command):
        monitor = _Monitor(self.monitor_socket)
        try:
            return monitor.command(command)
        finally:
            monitor.close()

    def _hard_reset(self):
        self._monitor("stop")
        # Let the controller take what the stopped firmware already sent.
        time.sleep(HCI_DRAIN_S)
        self._power_cycle_bluetooth()
        self._monitor("system_reset")
        self._monitor("cont")
        return True

    def default_connections(self):
        return [f"serial:socket://127.0.0.1:{self.console_port}"]

    def screenshot(self):
        from PIL import Image

        # Espressif's QEMU is built without libpng: PPM, which PIL reads too
        path = os.path.join(self._sockdir, "screendump.ppm")
        if os.path.exists(path):
            os.unlink(path)
        response = self._monitor(f"screendump {path}")
        if not os.path.isfile(path):
            raise HarnessError(f"QEMU wrote no screenshot: {response.strip()}")
        with Image.open(path) as image:
            shown = image.convert("RGB")
        # The machine shows the panel as its controller scans it, a quarter turn from upright,
        # with each framebuffer pixel scaled up: give the framebuffer, as the watch draws it
        shown = shown.transpose(Image.Transpose.ROTATE_90)
        size = (
            int(self.build.config.get("CONFIG_PLATFORM_DISPLAY_WIDTH") or shown.width),
            int(
                self.build.config.get("CONFIG_PLATFORM_DISPLAY_HEIGHT") or shown.height
            ),
        )
        if shown.size != size:
            shown = shown.resize(size, Image.Resampling.NEAREST)
        return shown
