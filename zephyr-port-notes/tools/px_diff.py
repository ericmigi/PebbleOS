#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0
"""Pixel-diff two PNGs (or two directories of same-named PNGs).

Exit 0 if identical, 1 otherwise; prints per-file differing pixel counts.
"""
import sys, os
from PIL import Image, ImageChops

def diff(a, b):
    ia, ib = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    if ia.size != ib.size:
        return -1
    bbox = ImageChops.difference(ia, ib).getbbox()
    if bbox is None:
        return 0
    d = ImageChops.difference(ia, ib)
    return sum(1 for p in d.getdata() if p != (0, 0, 0))

def main():
    a, b = sys.argv[1], sys.argv[2]
    rc = 0
    if os.path.isdir(a):
        names = sorted(set(os.listdir(a)) & set(os.listdir(b)))
        missing = sorted(set(os.listdir(a)) ^ set(os.listdir(b)))
        for name in names:
            n = diff(os.path.join(a, name), os.path.join(b, name))
            print("%-40s %s" % (name, "OK" if n == 0 else ("SIZE-MISMATCH" if n < 0 else "%d px differ" % n)))
            rc |= (n != 0)
        for name in missing:
            print("%-40s MISSING from one side" % name)
            rc = 1
    else:
        n = diff(a, b)
        print("OK" if n == 0 else "%s px differ" % n)
        rc = n != 0
    sys.exit(rc)

if __name__ == "__main__":
    main()
