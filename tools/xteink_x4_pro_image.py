#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0
"""Convert a photograph or graphic for the X4 Pro's 'sd show' command."""

import argparse
from pathlib import Path

from PIL import Image, ImageOps

SIZE = (480, 800)
HEADER = b"X4P2\x01\0\0\0"


def bayer_rank(x, y):
    rank = 0
    for _ in range(4):
        rank = (rank << 2) | (((x ^ y) & 1) << 1) | (y & 1)
        x >>= 1
        y >>= 1
    return rank


def encode(image):
    rgba = ImageOps.exif_transpose(image).convert("RGBA")
    white = Image.new("RGBA", rgba.size, "white")
    white.alpha_composite(rgba)
    frame = ImageOps.pad(white.convert("L"), SIZE, Image.Resampling.LANCZOS, color=255)
    ranks = [[bayer_rank(x, y) for x in range(16)] for y in range(16)]
    pixels = frame.load()
    planes = [bytearray(48000), bytearray(48000)]
    for x in range(480):
        # Portrait pixels map to the Pro's default mounting rotation of 270 degrees.
        gate = (479 - x) * 100
        for y in range(800):
            target = ((255 - pixels[x, y]) * 768 + 127) // 255
            tone, fraction = divmod(target, 256)
            tone += ranks[y % 16][x % 16] < fraction
            index = gate + y // 8
            mask = 128 >> (y % 8)
            if tone & 1:
                planes[0][index] |= mask
            if tone & 2:
                planes[1][index] |= mask
    return HEADER + planes[0] + planes[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument(
        "output", type=Path, help="Use an 8.3 filename on the SD card, e.g. PHOTO.X4P"
    )
    args = parser.parse_args()
    with Image.open(args.input) as image:
        data = encode(image)
    args.output.write_bytes(data)


if __name__ == "__main__":
    main()
