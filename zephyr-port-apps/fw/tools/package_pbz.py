#!/usr/bin/env python3

import argparse
import subprocess
import sys
import time
from pathlib import Path

PEBBLEOS_ROOT = Path(__file__).resolve().parents[3]
HWREV = "obelix_pvt"


def main() -> None:
    parser = argparse.ArgumentParser(description="Build the obelix slot-0 PBZ")
    parser.add_argument("firmware", type=Path, help="headered firmware.bin")
    parser.add_argument("output", type=Path, help="output .pbz")
    parser.add_argument("--commit", required=True)
    parser.add_argument("--version", default="v9.9.9-dev")
    parser.add_argument("--timestamp", type=int, default=int(time.time()))
    args = parser.parse_args()

    # mkbundle computes the manifest firmware CRC (STM32/legacy) over the whole
    # headered firmware.bin; that is the CRC PRF's PutBytes commit verifies, a
    # separate layer from the zlib CRC inside the pblboot header.
    timestamp = args.timestamp
    subprocess.run(
        [
            sys.executable,
            str(PEBBLEOS_ROOT / "tools" / "mkbundle.py"),
            "firmware",
            "--firmware",
            str(args.firmware),
            "--firmware-timestamp",
            str(timestamp),
            "--firmware-commit",
            args.commit,
            "--firmware-type",
            "normal",
            "--board",
            HWREV,
            "--firmware-version",
            args.version,
            "--firmware-slot",
            "0",
            "--outfile",
            str(args.output),
            "--verbose",
        ],
        check=True,
    )


if __name__ == "__main__":
    main()
