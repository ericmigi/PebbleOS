#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Flash the checksum-verified firmware and resources for one Xteink reader."""

import argparse
import hashlib
import shlex
import subprocess
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("board", choices=("xteink_x4", "xteink_x4_pro"))
    parser.add_argument("--port", required=True, help="reader's USB serial device")
    parser.add_argument(
        "--assets", type=Path, default=Path("."), help="release download directory"
    )
    parser.add_argument(
        "--dry-run", action="store_true", help="verify files and print the command"
    )
    args = parser.parse_args()
    chip = {"xteink_x4": "esp32c3", "xteink_x4_pro": "esp32s3"}[args.board]
    try:
        manifest = (args.assets / "SHA256SUMS").read_text().splitlines()
        sums = {}
        for line in manifest:
            if line.strip():
                digest, name = line.split(maxsplit=1)
                sums[Path(name.lstrip("*")).name] = digest
        command = [
            sys.executable,
            "-m",
            "esptool",
            "--chip",
            chip,
            "--port",
            args.port,
            "--after",
            "hard-reset",
            "write-flash",
            "--flash-mode",
            "keep",
            "--flash-freq",
            "keep",
            "--flash-size",
            "keep",
        ]
        for offset, suffix, limit in [
            (0, "pebbleos.bin", 0x280000),
            (0x300000, "system_resources.pbpack", 0x180000),
        ]:
            path = args.assets / f"{args.board}-{suffix}"
            data = path.read_bytes()
            if not 0 < len(data) <= limit:
                parser.error(f"image size is outside its partition: {path}")
            if sums.get(path.name) != hashlib.sha256(data).hexdigest():
                parser.error(f"no matching release checksum for {path}")
            command.extend((hex(offset), str(path)))
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    print(shlex.join(command), flush=True)
    if not args.dry_run:
        subprocess.run(command, check=True)


if __name__ == "__main__":
    main()
