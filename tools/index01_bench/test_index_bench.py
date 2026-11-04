# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0
"""Checks for index_bench.py's trace analysis; needs numpy and bumble, not a rig.

uv run --no-project --with numpy --with bumble python test_index_bench.py
"""

import os
import sys
from argparse import Namespace

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import index_bench as ib


def trace(runs, length_ms=3000, base=1.0, motor=25.0):
    ms = np.full(length_ms, base)
    for on, off in runs:
        ms[on:off] = motor
    return ms


# Short bursts split by short dips merge into one run before the length check.
assert ib.motor_edges(trace([(100, 140), (150, 190), (200, 240)])) == [(100, 240)]
# A lone short burst is not the motor.
assert ib.motor_edges(trace([(100, 130)])) == []

# Origin lies in both brackets: start call [10.0, 10.4], stop call [20.1, 20.3] with a 10 s
# trace gives [10.1, 10.3].
t0, err = ib.trace_origin((10.0, 10.4), (20.1, 20.3), 10.0)
assert abs(t0 - 10.2) < 1e-9 and abs(err - 0.1) < 1e-9

args = Namespace(release_s=3.0, match_slack_ms=200)
edges = [(1000, 3050)]
# The watch hears the real ring 20 ms before the dongle does.
assert ib.latencies([(1.02, 3.0)], 0.0, edges, args) == [
    {"down_ms": -20.0, "up_ms": 50.0}
]
# Release never heard: down is still timed, up stays missing.
assert ib.latencies([(0.9, None)], 0.0, edges, args) == [
    {"down_ms": 100.0, "up_ms": None}
]
# No motor run: missed.
assert ib.latencies([(5.0, 7.0)], 0.0, edges, args) == [
    {"down_ms": None, "up_ms": None}
]
print("ok")
