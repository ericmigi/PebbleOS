#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0
"""Scripted UI walk for qemu-pebble: sendkey + screenshot per step.

Usage: ui_walk.py --mon build/qemu-mon.sock --out /tmp/walk-ref [--steps STEPS]
STEPS: comma-separated keys (up/down/left/right), 'shot:<name>', or
'burst:<name>:<seconds>' — captures frames as fast as the monitor allows for
<seconds>, dedupes consecutive identical frames, and saves the distinct
sequence as <name>-f000.png, <name>-f001.png, ... For animation parity,
run the same walk on both firmwares and px_diff the frame sequences.
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
    m.cmd("sendkey left")  # back: wakes backlight, no navigation from watchface
    time.sleep(1.0)
    n = 0
    for step in args.steps.split(","):
        step = step.strip()
        if step.startswith("burst:"):
            _, name, secs = step.split(":")
            end = time.time() + float(secs)
            prev = None
            fi = 0
            tmp = os.path.abspath(os.path.join(args.out, "_burst.ppm"))
            while time.time() < end:
                m.cmd("screendump %s" % tmp)
                try:
                    data = open(tmp, "rb").read()
                except OSError:
                    continue
                if data and data != prev:
                    Image.open(tmp).save(os.path.join(args.out, "%02d-%s-f%03d.png" % (n, name, fi)))
                    fi += 1
                    prev = data
            if os.path.exists(tmp):
                os.unlink(tmp)
            n += 1
            print("burst", name, "%d distinct frames" % fi)
        elif step.startswith("shot:"):
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
