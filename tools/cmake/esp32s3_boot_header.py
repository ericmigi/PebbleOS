# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

"""Wrap the IRAM trampoline in the ESP32-S3 ROM's single-segment image format."""

import hashlib
import struct
import sys
from functools import reduce
from operator import xor
from pathlib import Path

payload = Path(sys.argv[1]).read_bytes()
load_addr = int(sys.argv[3], 0)
payload += bytes(-len(payload) % 4)

# DIO, 40 MHz, 4 MiB mapped flash, S3 chip ID 9, appended SHA-256.
header = struct.pack(
    "<4BIB3BHBHH4BB",
    0xE9,
    1,
    2,
    0x20,
    load_addr,
    0xEE,
    0,
    0,
    0,
    9,
    0,
    0,
    0xFFFF,
    0,
    0,
    0,
    0,
    1,
)
image = bytearray(header + struct.pack("<II", load_addr, len(payload)) + payload)
image.extend(bytes((15 - len(image)) % 16))
image.append(reduce(xor, payload, 0xEF))
image.extend(hashlib.sha256(image).digest())
if len(image) > 4096:
    raise ValueError("ESP32-S3 boot image exceeds the 4 KiB before Reset_Handler")

lines = ['.section .boot_header, "a"']
for start in range(0, len(image), 16):
    lines.append(".byte " + ", ".join(hex(x) for x in image[start : start + 16]))
lines.append(f".zero {4096 - len(image)}")
Path(sys.argv[2]).write_text("\n".join(lines) + "\n")
