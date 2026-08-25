#!/usr/bin/env python3
"""Reconstruct PNG screenshots from the firmware's UART framebuffer dump.

The obelix FW app (zephyr-port-apps/fw) emits the raw 8bpp Pebble framebuffer
over the console UART on every render, framed as:

    FB_BEGIN seq=<n> w=200 h=228 bpp=8 stride=200 size=45600
    FB <hex bytes>            (repeated, <=64 bytes per line)
    FB_END crc=0x........

The 8-bit pixels are Pebble GColor8 (ARGB2222): bits a[7:6] r[5:4] g[3:2] b[1:0],
each 2-bit channel scaled *85 to 8-bit. Alpha is treated as opaque.

Usage:
    fb_reconstruct.py CAPTURE.log OUTDIR [--prefix name]

Writes OUTDIR/<prefix>_seqNN.png for every complete, CRC-valid block, plus
prints a one-line summary per block. Requires Pillow (PIL).
"""
import argparse
import re
import sys
import zlib
from pathlib import Path

from PIL import Image

BEGIN_RE = re.compile(
    r"FB_BEGIN seq=(\d+) w=(\d+) h=(\d+) bpp=8 stride=(\d+) size=(\d+)")
END_RE = re.compile(r"FB_END crc=0x([0-9a-fA-F]+)")
DATA_RE = re.compile(r"FB ([0-9a-fA-F]+)\s*$")


def argb2222_to_rgb(byte):
    r = ((byte >> 4) & 0x3) * 85
    g = ((byte >> 2) & 0x3) * 85
    b = (byte & 0x3) * 85
    return r, g, b


def blocks(lines):
    """Yield (seq, w, h, stride, raw_bytes, crc_field) for each FB_BEGIN..FB_END."""
    it = iter(lines)
    for line in it:
        m = BEGIN_RE.search(line)
        if not m:
            continue
        seq, w, h, stride, size = (int(m.group(i)) for i in range(1, 6))
        data = bytearray()
        crc_field = None
        for inner in it:
            e = END_RE.search(inner)
            if e:
                crc_field = int(e.group(1), 16)
                break
            d = DATA_RE.search(inner)
            if d:
                try:
                    data.extend(bytes.fromhex(d.group(1)))
                except ValueError:
                    # A UART glitch corrupted this line; the CRC check below
                    # will flag the block. Keep going so a later good block wins.
                    pass
            elif BEGIN_RE.search(inner):
                # A new block started before this one ended: abandon this one.
                data = None
                break
        if data is None or crc_field is None:
            continue
        yield seq, w, h, stride, size, bytes(data), crc_field


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("capture")
    ap.add_argument("outdir")
    ap.add_argument("--prefix", default="shot")
    ap.add_argument("--scale", type=int, default=2, help="integer upscale for saved PNG")
    args = ap.parse_args()

    lines = Path(args.capture).read_text(errors="replace").splitlines()
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    saved = 0
    for seq, w, h, stride, size, raw, crc_field in blocks(lines):
        ok = len(raw) == size
        crc = zlib.crc32(raw) & 0xFFFFFFFF
        crc_ok = crc == crc_field
        status = "OK" if (ok and crc_ok) else \
            f"BAD(len {len(raw)}/{size} crc {crc:08x}/{crc_field:08x})"
        print(f"seq={seq} {w}x{h} stride={stride} {status}")
        if not ok:
            continue  # can't build an image from a short buffer
        img = Image.new("RGB", (w, h))
        px = img.load()
        for y in range(h):
            base = y * stride
            for x in range(w):
                px[x, y] = argb2222_to_rgb(raw[base + x])
        if args.scale > 1:
            img = img.resize((w * args.scale, h * args.scale), Image.NEAREST)
        tag = "" if crc_ok else "_crcbad"
        out = outdir / f"{args.prefix}_seq{seq:02d}{tag}.png"
        img.save(out)
        saved += 1
        print(f"  -> {out}")

    if saved == 0:
        print("no complete framebuffer blocks found", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
