#!/usr/bin/env python3

import argparse
import struct
import sys
import time
from pathlib import Path

PEBBLEOS_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(PEBBLEOS_ROOT / "tools"))
import stm32_crc  # noqa: E402

MAGIC = 0x96F3B83D
HEADER = struct.Struct("<IIQIII")
VECTOR_ALIGNMENT = 0x200


def main() -> None:
    parser = argparse.ArgumentParser(description="Add a pblboot FirmwareHeader")
    parser.add_argument("input", type=Path, help="raw zephyr.bin")
    parser.add_argument("output", type=Path, help="headered firmware.bin")
    parser.add_argument("--timestamp", type=int, default=int(time.time()))
    args = parser.parse_args()

    payload = args.input.read_bytes()
    fw_start = (HEADER.size + VECTOR_ALIGNMENT - 1) & -VECTOR_ALIGNMENT
    fw_crc = stm32_crc.crc32(payload) & 0xFFFFFFFF
    header = HEADER.pack(
        MAGIC, HEADER.size, args.timestamp, fw_start, len(payload), fw_crc
    )
    args.output.write_bytes(header + b"\xff" * (fw_start - HEADER.size) + payload)

    image = args.output.read_bytes()
    magic, header_length, _, start, length, expected_crc = HEADER.unpack_from(image)
    assert magic == MAGIC and header_length == HEADER.size
    assert len(image) == start + length
    assert stm32_crc.crc32(image[start : start + length]) == expected_crc
    print(
        f"self-check passed: fw_start=0x{start:x} fw_length={length} "
        f"fw_crc=0x{expected_crc:08x}"
    )


if __name__ == "__main__":
    main()
