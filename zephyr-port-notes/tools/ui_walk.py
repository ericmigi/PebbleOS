#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0
"""Scripted UI walk for qemu-pebble: sendkey + screenshot per step.

Usage: ui_walk.py --mon build/qemu-mon.sock --out /tmp/walk-ref [--steps STEPS]
STEPS: comma-separated keys (up/down/left/right) or 'shot:<name>'.
Default walk covers launcher scroll + a few app screens.
"""
import argparse, socket, time, os, sys
from PIL import Image

DEFAULT = ("shot:launcher-top,down,shot:l1,down,shot:l2,down,shot:l3,down,shot:l4,"
           "up,up,up,up,right,shot:settings,left,shot:launcher-back")

class Mon:
    def __init__(self, sock_path):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.connect(sock_path)
        self.s.settimeout(3)
        self._read_prompt()

    def _read_prompt(self):
        buf = b""
        try:
            while b"(qemu)" not in buf:
                buf += self.s.recv(4096)
        except socket.timeout:
            pass
        return buf

    def cmd(self, c):
        self.s.sendall((c + "\n").encode())
        self._read_prompt()

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mon", default="build/qemu-mon.sock")
    ap.add_argument("--out", required=True)
    ap.add_argument("--steps", default=DEFAULT)
    ap.add_argument("--settle", type=float, default=1.0)
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    m = Mon(args.mon)
    # wake the backlight so captures are in a consistent lit state
    m.cmd("sendkey ret")
    time.sleep(1.0)
    n = 0
    for step in args.steps.split(","):
        step = step.strip()
        if step.startswith("shot:"):
            name = "%02d-%s.png" % (n, step[5:])
            n += 1
            time.sleep(args.settle)
            ppm = os.path.abspath(os.path.join(args.out, name + ".ppm"))
            m.cmd("screendump %s" % ppm)
            time.sleep(0.5)
            Image.open(ppm).save(os.path.join(args.out, name))
            os.unlink(ppm)
            print("shot", name)
        else:
            m.cmd("sendkey %s" % step)
            time.sleep(0.4)
            print("key", step)
    print("done", args.out)

if __name__ == "__main__":
    main()
