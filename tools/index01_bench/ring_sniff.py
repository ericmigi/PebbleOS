# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0
"""Continuously scan with an HCI dongle and print every Index 01 advertisement with a host timestamp.

ring_sniff.py serial:/dev/cu.usbmodem114201,1000000 SECONDS
"""

import asyncio
import sys
import time

from bumble.core import AdvertisingData
from bumble.device import Device
from bumble.hci import Address
from bumble.transport import open_transport

INDEX_UUID = "607B5C9B-3700-4E94-F44A-2DF900BCB0C3"
COMPANY = 0x0EEA


def index_fields(adv):
    data = adv.data
    uuids = [
        str(u).upper()
        for u in (
            data.get(AdvertisingData.COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS) or []
        )
    ]
    mfg = data.get(AdvertisingData.MANUFACTURER_SPECIFIC_DATA)
    if INDEX_UUID not in uuids and not (mfg and mfg[0] == COMPANY):
        return None
    payload = mfg[1] if mfg and mfg[0] == COMPANY else b""
    return payload


async def main(transport, seconds):
    async with await open_transport(transport) as (source, sink):
        device = Device.with_hci("sniff", Address("F0:F1:F2:F3:F4:F5"), source, sink)
        seen = [0]

        def on_adv(adv):
            payload = index_fields(adv)
            seen[0] += 1
            if payload is None:
                return
            flags = payload[5] if len(payload) > 5 else -1
            print(
                f"{time.time():.3f} {adv.address} rssi={adv.rssi} type={adv.data_type if hasattr(adv, 'data_type') else ''}"
                f" scan_rsp={adv.is_scan_response} flags=0x{flags:02x} payload={payload.hex()}",
                flush=True,
            )

        device.on("advertisement", on_adv)
        await device.power_on()
        await device.start_scanning(
            active=False, scan_interval=30, scan_window=30, filter_duplicates=False
        )
        await asyncio.sleep(seconds)
        await device.stop_scanning()
        print(f"# {seen[0]} adverts total", flush=True)


asyncio.run(main(sys.argv[1], float(sys.argv[2])))
