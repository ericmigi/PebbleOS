#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

"""Serve a pbw's binary of one platform as that of another.

A phone picks a pbw's binary by the platform it takes the watch for: the
Xteink X4 reports itself as an Asterix, so phones install flint/, the ARM
binary, which the X4 refuses. This moves the xteinkx4/ directory (the X4's
RISC-V binary, worker and resources) to flint/ and drops the other platform
directories, so the phone installs the RISC-V binary, along with the pbw's
PebbleKit JS. The firmware runs a binary by the ABI its header records, not
by the directory it came from. For development, until phones know xteinkx4.

    tools/pbw_as_platform.py app.pbw app-x4.pbw [--from xteinkx4] [--to flint]
"""

import argparse
import json
import sys
import zipfile


def repack(src, out, frm="xteinkx4", to="flint"):
    with zipfile.ZipFile(src) as zin:
        names = zin.namelist()
        if not any(n.startswith(frm + "/") for n in names):
            raise ValueError(f"{src} has no {frm}/ binary")
        dirs = {n.split("/")[0] for n in names if "/" in n}
        with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zout:
            for n in names:
                top = n.split("/")[0] if "/" in n else None
                data = zin.read(n)
                if top in dirs:
                    if top != frm:
                        continue
                    if n.endswith("/manifest.json"):
                        # Of the directory's platform by its name, as phones that
                        # predate the platform description read it
                        manifest = json.loads(data)
                        manifest.pop("platform", None)
                        data = json.dumps(manifest).encode()
                    n = to + n[len(frm) :]
                elif n == "appinfo.json":
                    info = json.loads(data)
                    others = [
                        p for p in info.get("targetPlatforms", []) if p not in dirs
                    ]
                    info["targetPlatforms"] = [to] + others
                    data = json.dumps(info, indent=2).encode()
                zout.writestr(n, data)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("pbw")
    parser.add_argument("output")
    parser.add_argument(
        "--from", dest="frm", default="xteinkx4", help="platform to serve"
    )
    parser.add_argument("--to", default="flint", help="platform to serve it as")
    args = parser.parse_args()
    try:
        repack(args.pbw, args.output, args.frm, args.to)
    except ValueError as e:
        sys.exit(str(e))


if __name__ == "__main__":
    main()
