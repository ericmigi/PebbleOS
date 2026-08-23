#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

"""Package a linked pebble-app.elf into a .pbw.

Replays the post-link steps of the SDK waf pipeline (objcopy -> metadata
injection -> bundle) for an ELF produced outside waf, e.g. by the wasm
clang/lld toolchain. Resources, appinfo.json, and bundled JS are reused
from a prior `waf build` of the same project.

Usage:
  package-app.py --sdk-tools <sdk>/common/tools --build <project>/build \
      --platform gabbro --elf <pebble-app.elf> --out <out.pbw> \
      [--sdk-major 5] [--sdk-minor 122]
"""

import argparse
import glob
import os
import subprocess
import sys
import time


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sdk-tools", required=True)
    ap.add_argument("--build", required=True, help="project build dir from a prior waf run")
    ap.add_argument("--platform", required=True)
    ap.add_argument("--elf", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--sdk-major", type=int, default=None)
    ap.add_argument("--sdk-minor", type=int, default=None)
    args = ap.parse_args()

    sys.path.insert(0, args.sdk_tools)
    import inject_metadata
    import mkbundle

    build = os.path.abspath(args.build)
    pdir = os.path.join(build, args.platform)
    workdir = os.path.dirname(os.path.abspath(args.out))
    raw_bin = os.path.join(workdir, "pebble-app.raw.bin")
    app_bin = os.path.join(workdir, "pebble-app.bin")

    subprocess.run(
        ["arm-none-eabi-objcopy", "-S", "-R", ".stack", "-R", ".priv_bss",
         "-R", ".bss", "-R", ".retained", "-O", "binary", args.elf, raw_bin],
        check=True)

    resources = os.path.join(pdir, "app_resources.pbpack")
    js = sorted(glob.glob(os.path.join(build, "pebble-js-app.js*")))
    timestamp = int(time.time())

    with open(raw_bin, "rb") as f_in, open(app_bin, "wb") as f_out:
        f_out.write(f_in.read())
    inject_metadata.inject_metadata(
        app_bin, args.elf, resources, timestamp,
        allow_js=bool(js), has_worker=False)

    sdk_version = None
    if args.sdk_major is not None and args.sdk_minor is not None:
        sdk_version = {"major": args.sdk_major, "minor": args.sdk_minor}
    mkbundle.make_watchapp_bundle(
        timestamp=timestamp,
        appinfo=os.path.join(build, "appinfo.json"),
        binaries=[{
            "watchapp": app_bin,
            "resources": resources,
            "worker_bin": None,
            "sdk_version": sdk_version,
            "subfolder": args.platform,
        }],
        js=js,
        outfile=os.path.abspath(args.out))
    print("wrote", args.out)


if __name__ == "__main__":
    main()
