#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0
"""Run the same key script on two firmwares under the frame-recorder QEMU and
report per-frame pixel diffs per segment.

Usage:
  frame_walk.py --ref-elf build/pebbleos.elf --zephyr-elf <zephyr.elf> \
      --spi build/qemu_spi_flash.bin --qemu ~/dev/qemu-pebble-src/build/qemu-system-arm \
      --steps "settle:10,left,wait:2,right,wait:3,down,wait:2" --out /tmp/fwalk

Each key press starts a new named segment (frames recorded after it). Report:
per-segment frame counts both sides + per-frame px_diff (aligned by index).
"""
import argparse, os, shutil, socket, subprocess, sys, tempfile, time

def mon_cmd(sock_path, cmd, tries=20):
    for _ in range(tries):
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(sock_path)
            s.settimeout(2)
            try:
                s.recv(4096)
            except socket.timeout:
                pass
            s.sendall((cmd + "\n").encode())
            time.sleep(0.2)
            s.close()
            return
        except (ConnectionRefusedError, FileNotFoundError):
            time.sleep(1)
    raise RuntimeError("monitor not reachable: " + sock_path)

def frame_count(d):
    try:
        return len([f for f in os.listdir(d) if f.endswith(".ppm")])
    except FileNotFoundError:
        return 0

def run_side(name, qemu, elf, spi_src, steps, out, extra_machine, boot_marker_log=None):
    workdir = os.path.join(out, name)
    frames = os.path.join(workdir, "frames")
    os.makedirs(workdir, exist_ok=True)
    spi = os.path.join(workdir, "spi.bin")
    shutil.copy(spi_src, spi)
    sock = os.path.join(workdir, "mon.sock")
    log = os.path.join(workdir, "console.log")
    cmd = [qemu, "-rtc", "base=localtime", "-machine", "pebble-emery" + extra_machine,
           "-display", "none", "-kernel", elf,
           "-serial", "file:" + log, "-serial", "null", "-serial", "null",
           "-monitor", "unix:%s,server=on,wait=off" % sock,
           "-drive", "if=mtd,format=raw,file=" + spi,
           "-global", "pebble-display.record-dir=" + frames]
    if extra_machine:
        cmd += ["-audiodev", "none,id=snd0"]
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    segments = []  # (label, start_idx, end_idx)
    try:
        # wait for first frames (boot render)
        t0 = time.time()
        while frame_count(frames) < 1:
            if time.time() - t0 > 90:
                raise RuntimeError(name + ": no frames after 90s")
            time.sleep(1)
        prev_label = "boot"
        prev_idx = 0
        for step in steps:
            if step.startswith("settle:") or step.startswith("wait:"):
                time.sleep(float(step.split(":")[1]))
            else:
                n = frame_count(frames)
                segments.append((prev_label, prev_idx, n))
                prev_label, prev_idx = step, n
                mon_cmd(sock, "sendkey " + step)
        time.sleep(2)
        segments.append((prev_label, prev_idx, frame_count(frames)))
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()
    return frames, segments

def px_diff(a, b):
    from PIL import Image, ImageChops
    ia, ib = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    if ia.size != ib.size:
        return -1
    d = ImageChops.difference(ia, ib)
    if d.getbbox() is None:
        return 0
    return sum(1 for p in d.getdata() if p != (0, 0, 0))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref-elf", required=True)
    ap.add_argument("--zephyr-elf", required=True)
    ap.add_argument("--spi", required=True)
    ap.add_argument("--qemu", required=True)
    ap.add_argument("--steps", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    steps = [s.strip() for s in args.steps.split(",") if s.strip()]
    os.makedirs(args.out, exist_ok=True)
    rf, rseg = run_side("ref", args.qemu, args.ref_elf, args.spi, steps, args.out,
                        ",audiodev=snd0")
    zf, zseg = run_side("zephyr", args.qemu, args.zephyr_elf, args.spi, steps, args.out, "")
    rframes = sorted(f for f in os.listdir(rf) if f.endswith(".ppm"))
    zframes = sorted(f for f in os.listdir(zf) if f.endswith(".ppm"))
    exit_code = 0
    for (rl, r0, r1), (zl, z0, z1) in zip(rseg, zseg):
        rn, zn = r1 - r0, z1 - z0
        print("== segment %-12s ref=%d zephyr=%d frames%s" %
              (rl, rn, zn, "" if rn == zn else "  COUNT-MISMATCH"))
        if rn != zn:
            exit_code = 1
        for i in range(max(rn, zn)):
            ra = os.path.join(rf, rframes[r0 + i]) if i < rn else None
            za = os.path.join(zf, zframes[z0 + i]) if i < zn else None
            if ra and za:
                n = px_diff(ra, za)
                if n:
                    exit_code = 1
                print("   frame %2d: %s" % (i, "OK" if n == 0 else "%d px" % n))
            else:
                print("   frame %2d: MISSING on %s" % (i, "zephyr" if za is None else "ref"))
    sys.exit(exit_code)

if __name__ == "__main__":
    main()
