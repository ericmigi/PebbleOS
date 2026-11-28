# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

"""Install one or more compatible PBWs over the Xteink or T-Deck Pro firmware console."""

import argparse
import zipfile
from pathlib import Path
from unittest.mock import patch

from libpebble2.communication import PebbleConnection
from libpebble2.services.blobdb import BlobDBClient
from libpebble2.services.install import AppInstaller
from libpebble2.util.bundle import PebbleBundle
from pbl.emulator import identify_as_phone
from pblconsole import ConsoleLink
from pblconsole.protocol import FrameTransport


def variant(path, board):
    prefixes = {
        "xteink_x4": ("xteinkx4/",),
        "xteink_x4_pro": ("xteinkx4pro/", *PebbleBundle.PLATFORM_PATHS["flint"]),
        "t_deck_pro": ("tdeckpro/",),
    }[board]
    with zipfile.ZipFile(path) as archive:
        names = set(archive.namelist())
        for prefix in prefixes:
            if {prefix + "manifest.json", prefix + "pebble-app.bin"} <= names:
                return prefix
    raise ValueError(f"{path} has no compatible application for {board}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--port", required=True, help="Serial device or socket://HOST:PORT"
    )
    parser.add_argument(
        "--board", choices=("xteink_x4", "xteink_x4_pro", "t_deck_pro"), default="xteink_x4_pro"
    )
    parser.add_argument("pbw", type=Path, nargs="+")
    args = parser.parse_args()
    try:
        selected = [(path, variant(path, args.board)) for path in args.pbw]
    except (OSError, ValueError, zipfile.BadZipFile) as exc:
        parser.error(str(exc))

    link = ConsoleLink(args.port)
    link.open()
    try:
        transport = FrameTransport(link)
        try:
            pebble = PebbleConnection(transport)
            pebble.connect()
            pebble.run_async()
            identify_as_phone(pebble)
            database = BlobDBClient(pebble)
            for path, prefix in selected:
                # Resolve the app, optional worker and resources from the same variant.
                with patch.dict(
                    PebbleBundle.PLATFORM_PATHS, {pebble.watch_platform: (prefix,)}
                ):
                    AppInstaller(pebble, str(path), blobdb_client=database).install()
                print(
                    f"Installed {path} ({prefix}pebble-app.bin) on {args.port}",
                    flush=True,
                )
        finally:
            transport.disconnect()
    finally:
        link.close()


if __name__ == "__main__":
    main()
