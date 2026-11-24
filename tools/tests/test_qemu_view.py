# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "libs" / "pbl-cli"))

from pbl.qemu_view import upright


def ppm(width, height, values):
    return b"P6\n%d %d\n255\n" % (width, height) + b"".join(
        bytes((v, 0, 0)) for v in values
    )


def test_landscape_turns_upright():
    # 4x2, the right column becomes the top row
    assert upright(ppm(4, 2, range(1, 9)), 1) == b"P5 2 4 255\n" + bytes(
        (4, 8, 3, 7, 2, 6, 1, 5)
    )


def test_portrait_kept_and_halved():
    # The first pixel a whitespace byte, which must not be taken for the header
    image = ppm(2, 4, (9, 10, 11, 12, 13, 14, 15, 16))
    assert upright(image, 1) == b"P5 2 4 255\n" + bytes(range(9, 17))
    assert upright(image, 2) == b"P5 1 2 255\n" + bytes((9, 13))
