#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0
"""Write the ROM symbols a link needs, picked from ESP-IDF's ROM linker scripts.

Usage: gen_rom_ld.py SYMBOLS IDF_LD... > rom.ld

SYMBOLS lists the names the link leaves undefined without rom.ld, one per line, e.g. the
"undefined reference" names of a failed link. The scripts are read in the order given, a later
one overriding an earlier one, as ESP-IDF links an ECO's script after the base one.
"""

import os
import re
import sys
from pathlib import Path

ASSIGN = re.compile(
    r"^\s*(?:PROVIDE\s*\(\s*)?([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(0x[0-9a-fA-F]+)\s*\)?\s*;"
)


def main(symbols_path, scripts):
    wanted = sorted({s for s in Path(symbols_path).read_text().split() if s})
    found = {}
    for script in scripts:
        for line in Path(script).read_text().splitlines():
            m = ASSIGN.match(line)
            if m and m.group(1) in wanted:
                found[m.group(1)] = (m.group(2), os.path.basename(script))
    missing = [s for s in wanted if s not in found]
    if missing:
        sys.exit("not in the ROM scripts: " + " ".join(missing))

    print("/*")
    print(" * SPDX-FileCopyrightText: Espressif Systems (Shanghai) CO LTD")
    print(" * SPDX-License-Identifier: Apache-2.0")
    print(" *")
    print(" * The ESP32-C3 ROM symbols the Bluetooth libraries use, from ESP-IDF 5.5.5")
    print(" * components/esp_rom/esp32c3/ld, written by gen_rom_ld.py: do not edit.")
    print(" */")
    for source in sorted({src for _, src in found.values()}):
        print(f"\n/* {source} */")
        for name in wanted:
            addr, src = found[name]
            if src == source:
                print(f"PROVIDE({name} = {addr});")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2:])
