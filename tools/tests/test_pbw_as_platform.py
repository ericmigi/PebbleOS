# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

import json
import os
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import pbw_as_platform


class TestPbwAsPlatform(unittest.TestCase):
    def test_serves_xteinkx4_as_flint(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = os.path.join(tmp, "app.pbw")
            out = os.path.join(tmp, "x4.pbw")
            platform = {"name": "xteinkx4", "abi": "riscv32"}
            with zipfile.ZipFile(src, "w") as z:
                z.writestr(
                    "appinfo.json",
                    json.dumps({"targetPlatforms": ["flint", "xteinkx4"]}),
                )
                z.writestr("pebble-js-app.js", "// js")
                z.writestr("assets/phone.json", "{}")
                z.writestr("flint/pebble-app.bin", b"arm")
                z.writestr("xteinkx4/pebble-app.bin", b"riscv")
                z.writestr("xteinkx4/app_resources.pbpack", b"res")
                z.writestr(
                    "xteinkx4/manifest.json", json.dumps({"platform": platform, "a": 1})
                )

            with self.assertRaises(ValueError):
                pbw_as_platform.repack(src, src)
            pbw_as_platform.repack(src, out)

            with zipfile.ZipFile(out) as z:
                self.assertEqual(
                    sorted(z.namelist()),
                    [
                        "appinfo.json",
                        "assets/phone.json",
                        "flint/app_resources.pbpack",
                        "flint/manifest.json",
                        "flint/pebble-app.bin",
                        "pebble-js-app.js",
                    ],
                )
                self.assertEqual(z.read("flint/pebble-app.bin"), b"riscv")
                self.assertEqual(json.loads(z.read("flint/manifest.json")), {"a": 1})
                self.assertEqual(
                    json.loads(z.read("appinfo.json"))["targetPlatforms"], ["flint"]
                )

    def test_refuses_a_pbw_without_the_platform(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = os.path.join(tmp, "app.pbw")
            with zipfile.ZipFile(src, "w") as z:
                z.writestr("flint/pebble-app.bin", b"arm")
            with self.assertRaises(ValueError):
                pbw_as_platform.repack(src, os.path.join(tmp, "x4.pbw"))


if __name__ == "__main__":
    unittest.main()
