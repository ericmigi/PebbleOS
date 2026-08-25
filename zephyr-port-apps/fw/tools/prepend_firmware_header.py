#!/usr/bin/env python3
"""Prepend the pblboot firmware header to a raw Zephyr slot-0 image.

Matches the contract in pblboot's boot/src/firmware.c and the shipping packager
tools/waf/pblboot.py: a 28-byte header at the slot base, the firmware body at
slot_base + FIRMWARE_OFFSET (0x1000), and a standard IEEE/zlib CRC-32 over the
body (NOT the STM32 hardware CRC). pblboot validates magic + header_length==28,
then recomputes zlib CRC over [base+start_offset, +length) and boots the valid
slot with the highest 64-bit `priority`.
"""

import argparse
import struct
import time
import zlib
from pathlib import Path

MAGIC = 0x96F3B83D
HEADER = struct.Struct("<LLQLLL")  # magic, header_len, priority, start_off, len, crc
FIRMWARE_OFFSET = 0x1000
PRIORITY_BAND_DEV = 0x80


def main() -> None:
    parser = argparse.ArgumentParser(description="Add a pblboot firmware header")
    parser.add_argument("input", type=Path, help="raw zephyr.bin (body only)")
    parser.add_argument("output", type=Path, help="headered firmware.bin")
    parser.add_argument("--timestamp", type=int, default=int(time.time()))
    parser.add_argument("--offset", type=int, default=FIRMWARE_OFFSET)
    args = parser.parse_args()

    payload = args.input.read_bytes()
    # Dev-band priority so the freshest dev build always out-ranks the other slot.
    priority = (PRIORITY_BAND_DEV << 56) | (args.timestamp & ((1 << 56) - 1))
    fw_crc = zlib.crc32(payload) & 0xFFFFFFFF
    header = HEADER.pack(
        MAGIC, HEADER.size, priority, args.offset, len(payload), fw_crc
    )
    assert args.offset >= HEADER.size, "offset must leave room for the header"
    args.output.write_bytes(header + b"\xff" * (args.offset - HEADER.size) + payload)

    # Re-read and validate exactly as pblboot will.
    image = args.output.read_bytes()
    magic, hlen, pri, start, length, expected_crc = HEADER.unpack_from(image)
    assert magic == MAGIC and hlen == HEADER.size
    assert len(image) == start + length
    assert zlib.crc32(image[start : start + length]) & 0xFFFFFFFF == expected_crc
    print(
        f"self-check passed: start_offset=0x{start:x} length={length} "
        f"priority=0x{pri:016x} crc=0x{expected_crc:08x}"
    )


if __name__ == "__main__":
    main()
