#!/usr/bin/env python3

import argparse
import struct
import subprocess
import sys
from pathlib import Path

PEBBLEOS_ROOT = Path(__file__).resolve().parents[3]
HWREV = "obelix_pvt"


def main() -> None:
    parser = argparse.ArgumentParser(description="Build the obelix slot-0 PBZ")
    parser.add_argument("firmware", type=Path, help="headered firmware.bin")
    parser.add_argument("output", type=Path, help="output .pbz")
    parser.add_argument("--commit", required=True)
    parser.add_argument("--version", default="v9.9.9-dev")
    args = parser.parse_args()

    timestamp = struct.unpack_from("<Q", args.firmware.read_bytes(), 8)[0]
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
