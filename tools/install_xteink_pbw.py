# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

"""Install a native X4 Pro or compatible ARM PBW over the USB or emulated firmware console."""

import argparse
import zipfile

from libpebble2.communication import PebbleConnection
from libpebble2.services.install import AppInstaller
from libpebble2.util.bundle import PebbleBundle
from pbl.emulator import identify_as_phone
from pblconsole import ConsoleLink
from pblconsole.protocol import FrameTransport


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--port", required=True, help="Serial device or socket://HOST:PORT"
    )
    parser.add_argument("pbw", help="PBW containing a native or Flint-compatible application")
    args = parser.parse_args()

    link = ConsoleLink(args.port)
    link.open()
    try:
        transport = FrameTransport(link)
        try:
            pebble = PebbleConnection(transport)
            pebble.connect()
            pebble.run_async()
            identify_as_phone(pebble)
            # libpebble2 maps the firmware's legacy hardware ID to an ARM platform.
            # Pick a complete variant up front; optional files must come from that same directory.
            with zipfile.ZipFile(args.pbw) as archive:
                prefixes = ("xteinkx4pro/", *PebbleBundle.PLATFORM_PATHS["flint"])
                selected = next(
                    (p for p in prefixes if p + "manifest.json" in archive.namelist()),
                    None,
                )
                if selected is None:
                    parser.error("PBW has no native X4 Pro or compatible ARM variant")
            PebbleBundle.PLATFORM_PATHS[pebble.watch_platform] = (selected,)
            AppInstaller(pebble, args.pbw).install()
        finally:
            transport.disconnect()
    finally:
        link.close()
    print(f"Installed {args.pbw} on {args.port}")


if __name__ == "__main__":
    main()
