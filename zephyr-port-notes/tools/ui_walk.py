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

def mon(sock_path, cmd):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(sock_path)
    s.settimeout(2)
    try:
        s.recv(4096)
    except socket.timeout:
        pass
    s.sendall((cmd + "\n").encode())
    time.sleep(0.3)
    try:
        s.recv(4096)
    except socket.timeout:
        pass
    s.close()

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mon", default="build/qemu-mon.sock")
    ap.add_argument("--out", required=True)
    ap.add_argument("--steps", default=DEFAULT)
    ap.add_argument("--settle", type=float, default=1.0)
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    n = 0
    for step in args.steps.split(","):
        step = step.strip()
        if step.startswith("shot:"):
            name = "%02d-%s.png" % (n, step[5:])
            n += 1
            time.sleep(args.settle)
            ppm = os.path.abspath(os.path.join(args.out, name + ".ppm"))
            mon(args.mon, "screendump %s" % ppm)
            time.sleep(0.5)
            Image.open(ppm).save(os.path.join(args.out, name))
            os.unlink(ppm)
            print("shot", name)
        else:
            mon(args.mon, "sendkey %s" % step)
            time.sleep(0.4)
            print("key", step)
    print("done", args.out)

if __name__ == "__main__":
    main()
