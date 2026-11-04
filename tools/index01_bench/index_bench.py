# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0
"""Index 01 scan bench: power and press latency of the watch's ring scan, with an nRF52840 HCI
dongle playing the ring and the PPK2 both powering the watch and seeing its vibration motor.

    index_bench.py --name CFG --out DIR [--presses 12] [--adv-ms 50] [--idle-s 60]

The watch must already run the firmware under test. Steps: power on, wait for the phone to
connect, settle, measure idle current, then fake button presses: the dongle advertises the
ring's packet with RECORDING set for --press-s, then cleared for --release-s, then goes quiet
for a random gap. Each press's latency is the time from the dongle starting to advertise to
the motor starting (DOWN), and from the release advert to the motor stopping (UP), both seen
as current in the PPK2 trace on the same clock. Writes DIR/CFG.json.
"""

import argparse
import asyncio
import itertools
import json
import os
import random
import subprocess
import sys
import time

import numpy as np
from bumble.core import AdvertisingData
from bumble.device import Device
from bumble.hci import Address, OwnAddressType
from bumble.transport import open_transport

DONGLE = "serial:/dev/cu.usbmodem114201,1000000"
ADB = os.path.expanduser("~/Library/Android/sdk/platform-tools/adb")
PHONE = "33141JEHN24410"
PBL = os.path.expanduser("~/pbl/bin/pbl")
RING_ADDR = "C4:15:0E:01:00:71"  # static random
UUID_LE = bytes.fromhex("c3b0bc00f92d4af4944e00379b5c7b60")
FINGERPRINT = bytes.fromhex("0eea0071")
RECORDING = 0x20
MOTOR_MA = 14.0  # 10 ms mean above this is the motor (~22 mA over baseline)
MOTOR_BINS = 5  # ... held for 5 bins (50 ms)


def adv_data(flags):
    mfg = (0x0EEA).to_bytes(2, "little") + FINGERPRINT + bytes([0, flags])
    return bytes(
        AdvertisingData(
            [
                (AdvertisingData.FLAGS, bytes([0x06])),
                (AdvertisingData.COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS, UUID_LE),
                (AdvertisingData.MANUFACTURER_SPECIFIC_DATA, mfg),
            ]
        )
    )


SCAN_RSP = bytes(
    AdvertisingData([(AdvertisingData.COMPLETE_LOCAL_NAME, b"Pebble Index 071")])
)


def phone_connected_since(t_wall):
    out = subprocess.run(
        [ADB, "-s", PHONE, "shell", "dumpsys", "bluetooth_manager"],
        check=False,
        capture_output=True,
        text=True,
    ).stdout
    for line in out.splitlines():
        parts = line.split()
        if (
            len(parts) >= 3
            and parts[2] in ("CONNECTED", "DISCONNECTED")
            and parts[0].count("-") == 1
        ):
            stamp = time.mktime(
                time.strptime(
                    f"{time.localtime().tm_year}-{parts[0]} {parts[1][:8]}",
                    "%Y-%m-%d %H:%M:%S",
                )
            )
            return parts[2] == "CONNECTED" and stamp >= t_wall - 1
    return False


def pbl(*args):
    return subprocess.run(
        [PBL, "power", *args], check=True, capture_output=True, text=True
    ).stdout


def motor_edges(ms):
    """(on_ms, off_ms) motor runs in a trace of 1 ms means (mA): 10 ms means above MOTOR_MA,
    segments split by dips under 50 ms merged, then runs shorter than MOTOR_BINS bins dropped."""
    bins = ms[: len(ms) // 10 * 10].reshape(-1, 10).mean(1)
    segments, start = [], None
    for i, h in enumerate(np.append(bins > MOTOR_MA, False)):
        if h and start is None:
            start = i
        elif not h and start is not None:
            segments.append((start * 10, i * 10))
            start = None
    merged = []
    for on, off in segments:
        if merged and on - merged[-1][1] < 50:
            merged[-1] = (merged[-1][0], off)
        else:
            merged.append((on, off))
    return [(on, off) for on, off in merged if off - on >= MOTOR_BINS * 10]


def trace_origin(start_call, stop_call, duration_s):
    """Monotonic time of the trace's first sample, and its uncertainty (s). Sampling starts
    somewhere inside the `pbl power start` call and stops inside the `stop` call, so the origin
    lies in both start_call and stop_call shifted back by the trace's length."""
    lo = max(start_call[0], stop_call[0] - duration_s)
    hi = min(start_call[1], stop_call[1] - duration_s)
    if lo > hi:
        lo, hi = start_call
    return (lo + hi) / 2, (hi - lo) / 2


async def run_presses(ring, args, rng):
    events = []
    for _ in range(args.presses):
        await asyncio.sleep(rng.uniform(args.gap_min_s, args.gap_max_s))
        t_down = time.monotonic()
        await ring.start_advertising(
            own_address_type=OwnAddressType.RANDOM,
            advertising_data=adv_data(RECORDING),
            scan_response_data=SCAN_RSP,
            advertising_interval_min=args.adv_ms,
            advertising_interval_max=args.adv_ms,
        )
        await asyncio.sleep(args.press_s)
        await ring.stop_advertising()
        t_up = time.monotonic()
        await ring.start_advertising(
            own_address_type=OwnAddressType.RANDOM,
            advertising_data=adv_data(0),
            scan_response_data=SCAN_RSP,
            advertising_interval_min=args.adv_ms,
            advertising_interval_max=args.adv_ms,
        )
        await asyncio.sleep(args.release_s)
        await ring.stop_advertising()
        events.append((t_down, t_up))
    await asyncio.sleep(2)
    return events


async def sniff_real_presses(dongle, args):
    """Listen for the real ring; return (t_down, t_up) of its first RECORDING / cleared adverts
    per press, and the gaps between consecutive adverts of the same state."""
    seen = []  # (t, recording)

    def on_adv(adv):
        mfg = adv.data.get(AdvertisingData.MANUFACTURER_SPECIFIC_DATA)
        if mfg and mfg[0] == 0x0EEA and len(mfg[1]) >= 6:
            seen.append((time.monotonic(), bool(mfg[1][5] & RECORDING)))

    dongle.on("advertisement", on_adv)
    await dongle.start_scanning(
        active=False, scan_interval=30, scan_window=30, filter_duplicates=False
    )
    print(
        f"press the ring {args.real} times (hold ~2 s, wait ~5 s between)", flush=True
    )
    end = time.monotonic() + args.real_timeout_s
    while time.monotonic() < end:
        await asyncio.sleep(0.5)
        if (
            sum(1 for a, b in itertools.pairwise(seen) if b[1] and not a[1])
            + (1 if seen and seen[0][1] else 0)
            >= args.real
            and seen[-1][1] is False
            and time.monotonic() - seen[-1][0] > 3
        ):
            break
    await dongle.stop_scanning()
    events, gaps, prev = [], [], None
    for t, rec in seen:
        if prev is not None and rec == prev[1]:
            gaps.append((t - prev[0]) * 1000)
        if rec and (prev is None or not prev[1]):
            events.append([t, None])
        elif not rec and prev is not None and prev[1] and events:
            events[-1][1] = t
        prev = (t, rec)
    return [tuple(e) for e in events], gaps


def latencies(events, t0, edges, args):
    """Match each (t_down, t_up) to the first motor run overlapping it. A run may start up to
    --match-slack-ms before t_down: a sniffing dongle can hear the real ring after the watch
    does, which shows as a negative latency. t_up None (release never heard) leaves up_ms None
    and closes the press at the motor's 10 s auto-stop."""
    rows = []
    for t_down, t_up in events:
        down_ms = (t_down - t0) * 1000
        up_ms = None if t_up is None else (t_up - t0) * 1000
        end_ms = down_ms + 10_000 if up_ms is None else up_ms + args.release_s * 1000
        on = next(
            (
                e
                for e in edges
                if e[1] > down_ms - args.match_slack_ms and e[0] < end_ms
            ),
            None,
        )
        rows.append(
            {
                "down_ms": None if on is None else on[0] - down_ms,
                "up_ms": None
                if on is None or up_ms is None or on[1] < up_ms
                else on[1] - up_ms,
            }
        )
    return rows


def pct(vals, q):
    vals = [v for v in vals if v is not None]
    return round(float(np.percentile(vals, q)), 1) if vals else None


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--presses", type=int, default=12)
    ap.add_argument("--adv-ms", type=float, default=50)
    ap.add_argument("--press-s", type=float, default=2.0)
    ap.add_argument("--release-s", type=float, default=3.0)
    ap.add_argument("--gap-min-s", type=float, default=4.0)
    ap.add_argument("--gap-max-s", type=float, default=9.0)
    ap.add_argument("--idle-s", type=float, default=60)
    ap.add_argument("--settle-s", type=float, default=60)
    ap.add_argument(
        "--match-slack-ms",
        type=float,
        default=200,
        help="motor may start this long before the press it is matched to",
    )
    ap.add_argument(
        "--no-power-cycle", action="store_true", help="watch already on and settled"
    )
    ap.add_argument(
        "--keep-on", action="store_true", help="leave the watch powered afterwards"
    )
    ap.add_argument(
        "--real",
        type=int,
        default=0,
        help="time N presses of the real ring (dongle sniffs)",
    )
    ap.add_argument("--real-timeout-s", type=float, default=180)
    args = ap.parse_args()
    rng = random.Random(args.name)
    result = {"name": args.name, "adv_ms": args.adv_ms}

    os.makedirs(args.out, exist_ok=True)
    try:
        if not args.no_power_cycle:
            pbl("off")
            await asyncio.sleep(1.5)
            t_on = time.time()
            pbl("on", "--voltage", "3800")
            for _ in range(60):
                if phone_connected_since(t_on):
                    break
                await asyncio.sleep(3)
            else:
                raise SystemExit("phone never connected")
            print(
                f"phone connected after {time.time() - t_on:.0f} s, settling {args.settle_s} s",
                flush=True,
            )
            await asyncio.sleep(args.settle_s)

        idle = json.loads(pbl("measure", "--seconds", str(args.idle_s), "--json"))
        result.update(
            idle_mean_ua=idle["mean_ua"], idle_p99_ua=idle["p99_ua"], idle_s=args.idle_s
        )
        print(f"idle: mean {idle['mean_ua']} uA", flush=True)

        if args.presses or args.real:
            async with await open_transport(DONGLE) as (source, sink):
                ring = Device.with_hci("ring", Address(RING_ADDR), source, sink)
                await ring.power_on()
                name = f"{args.name}-presses"
                start_call = (time.monotonic(), None)
                pbl("start", "--name", name)
                start_call = (start_call[0], time.monotonic())
                try:
                    if args.real:
                        events, gaps = await sniff_real_presses(ring, args)
                        args.presses = len(events)
                        gaps = [g for g in gaps if g < 1000]
                        result.update(
                            real=True,
                            ring_adv_gap_ms_p10=pct(gaps, 10),
                            ring_adv_gap_ms_p50=pct(gaps, 50),
                            ring_adv_gap_ms_p90=pct(gaps, 90),
                        )
                        print(
                            f"real ring: {len(events)} presses, advert gap p10/p50/p90 "
                            f"{pct(gaps, 10)}/{pct(gaps, 50)}/{pct(gaps, 90)} ms",
                            flush=True,
                        )
                    else:
                        events = await run_presses(ring, args, rng)
                finally:
                    stop_call = (time.monotonic(), None)
                    pbl("stop", "--save", args.out)
                    stop_call = (stop_call[0], time.monotonic())
            ms = (
                np.loadtxt(
                    os.path.join(args.out, f"{name}.csv"), delimiter=",", skiprows=1
                )[:, 1]
                / 1000
            )
            edges = motor_edges(ms)
            t0, t0_err = trace_origin(start_call, stop_call, len(ms) / 1000)
            result["clock_uncertainty_ms"] = round(t0_err * 1000, 1)
            np.savez_compressed(
                os.path.join(args.out, f"{args.name}-trace.npz"),
                ms=ms.astype(np.float32),
                events=np.array(
                    [
                        [(d - t0) * 1000, np.nan if u is None else (u - t0) * 1000]
                        for d, u in events
                    ]
                ),
            )
            os.remove(os.path.join(args.out, f"{name}.csv"))
            rows = latencies(events, t0, edges, args)
            downs = [r["down_ms"] for r in rows]
            ups = [r["up_ms"] for r in rows]
            result.update(
                presses=args.presses,
                motor_runs=len(edges),
                missed=sum(d is None for d in downs),
                down_ms=downs,
                up_ms=ups,
                down_p50=pct(downs, 50),
                down_p90=pct(downs, 90),
                down_max=pct(downs, 100),
                up_p50=pct(ups, 50),
                up_p90=pct(ups, 90),
                up_max=pct(ups, 100),
            )
            print(
                f"presses: missed {result['missed']}/{args.presses}, down p50 {result['down_p50']} "
                f"p90 {result['down_p90']} max {result['down_max']} ms, up p50 {result['up_p50']} "
                f"p90 {result['up_p90']} ms, motor runs {len(edges)}, "
                f"clock +/- {result['clock_uncertainty_ms']} ms",
                flush=True,
            )
    finally:
        if not args.keep_on:
            pbl("off")

    with open(os.path.join(args.out, f"{args.name}.json"), "w") as f:  # noqa: ASYNC230
        json.dump(result, f, indent=1)


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
