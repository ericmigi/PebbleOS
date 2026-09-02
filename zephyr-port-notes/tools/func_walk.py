#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0
"""Functional parity walk: run an interaction script on both firmwares, with
screenshots and an optional reboot phase (same SPI image), then pixel-diff
every screenshot pair.

Script format (--steps): comma-separated tokens
  <key>            sendkey (left/right/up/down)
  wait:<s>         sleep
  shot:<name>      screenshot
  reboot           terminate QEMU, relaunch with the same SPI image
Exit 0 iff every screenshot pair matches.
"""
import argparse, os, shutil, socket, subprocess, sys, time

QDEF = os.path.expanduser("~/dev/qemu-pebble-src/build/qemu-system-arm")

class Side:
    def __init__(self, name, qemu, elf, spi_src, out, extra):
        self.name, self.qemu, self.elf, self.extra = name, qemu, elf, extra
        self.dir = os.path.join(out, name)
        os.makedirs(self.dir, exist_ok=True)
        self.spi = os.path.join(self.dir, "spi.bin")
        shutil.copy(spi_src, self.spi)
        self.sock_path = os.path.join(self.dir, "mon.sock")
        self.proc = None
        self.sock = None

    def launch(self):
        if os.path.exists(self.sock_path):
            os.unlink(self.sock_path)
        cmd = [self.qemu, "-rtc", "base=localtime",
               "-machine", "pebble-emery" + self.extra, "-display", "none",
               "-kernel", self.elf,
               "-serial", "file:" + os.path.join(self.dir, "console.log"),
               "-serial", "null", "-serial", "null",
               "-monitor", "unix:%s,server=on,wait=off" % self.sock_path,
               "-drive", "if=mtd,format=raw,file=" + self.spi]
        if self.extra:
            cmd += ["-audiodev", "none,id=snd0"]
        self.proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL,
                                     stderr=subprocess.DEVNULL)
        time.sleep(14)
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(self.sock_path)
        self.sock.settimeout(2)
        self._drain()

    def _drain(self):
        buf = b""
        try:
            while b"(qemu)" not in buf:
                buf += self.sock.recv(4096)
        except socket.timeout:
            pass

    def cmd(self, c):
        self.sock.sendall((c + "\n").encode())
        self._drain()

    def stop(self):
        if self.proc:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
            self.proc = None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref-elf", required=True)
    ap.add_argument("--zephyr-elf", required=True)
    ap.add_argument("--spi", required=True)
    ap.add_argument("--qemu", default=QDEF)
    ap.add_argument("--steps", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    steps = [t.strip() for t in args.steps.split(",") if t.strip()]
    sides = [Side("ref", args.qemu, args.ref_elf, args.spi, args.out, ",audiodev=snd0"),
             Side("zephyr", args.qemu, args.zephyr_elf, args.spi, args.out, "")]
    shots = []
    for side in sides:
        side.launch()
    try:
        for tok in steps:
            if tok.startswith("wait:"):
                time.sleep(float(tok.split(":")[1]))
            elif tok.startswith("shot:"):
                name = tok.split(":")[1]
                shots.append(name)
                for side in sides:
                    side.cmd("screendump %s" % os.path.join(side.dir, name + ".ppm"))
                time.sleep(0.8)
            elif tok == "reboot":
                for side in sides:
                    side.stop()
                for side in sides:
                    side.launch()
            else:
                for side in sides:
                    side.cmd("sendkey " + tok)
                time.sleep(0.4)
    finally:
        for side in sides:
            side.stop()
    from PIL import Image, ImageChops
    rc = 0
    for name in shots:
        a = Image.open(os.path.join(sides[0].dir, name + ".ppm")).convert("RGB")
        b = Image.open(os.path.join(sides[1].dir, name + ".ppm")).convert("RGB")
        d = ImageChops.difference(a, b)
        n = 0 if d.getbbox() is None else sum(1 for p in d.getdata() if p != (0, 0, 0))
        print("%-24s %s" % (name, "OK" if n == 0 else "%d px %s" % (n, d.getbbox())))
        rc |= (n != 0)
    sys.exit(rc)

if __name__ == "__main__":
    main()
