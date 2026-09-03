#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0
"""Inject a fake notification into a running Pebble QEMU (either the FreeRTOS
reference or the Zephyr port) over the pebble-tool serial channel (BlobDB
insert into the Notifications DB), the same path CoreApp/Android uses.

Run under pebble-tool's interpreter (libpebble2):
  /home/taz/.local/share/uv/tools/pebble-tool/bin/python inject_notification.py \
      --port 12344 --title "Alice" --body "Hey, dinner at 7?"
"""
import argparse
import struct
import time
import uuid

from libpebble2.communication import PebbleConnection
from libpebble2.communication.transports.qemu import QemuTransport
from libpebble2.protocol.blobdb import BlobDatabaseID
from libpebble2.services.blobdb import BlobDBClient

ATTR_TITLE = 1
ATTR_SUBTITLE = 2
ATTR_BODY = 3
ATTR_ICON_TINY = 4

TIMELINE_TYPE_NOTIFICATION = 1
# Reserved system icon (GENERIC_EMAIL) — a stable known resource on both builds.
DEFAULT_ICON = 0x80000000 | 4  # TimelineResourceId flag + generic notification


def _attr(attr_id, content):
    return struct.pack('<BH', attr_id, len(content)) + content


def build_notification(title, subtitle, body, icon):
    attrs = []
    if title:
        attrs.append(_attr(ATTR_TITLE, title.encode('utf-8')))
    if subtitle:
        attrs.append(_attr(ATTR_SUBTITLE, subtitle.encode('utf-8')))
    if body:
        attrs.append(_attr(ATTR_BODY, body.encode('utf-8')))
    attrs.append(_attr(ATTR_ICON_TINY, struct.pack('<I', icon)))
    attr_blob = b''.join(attrs)

    item_id = uuid.uuid4()
    parent_id = uuid.UUID(int=0)
    timestamp = int(time.time())
    duration = 0
    flags = 0
    layout = 0x01  # NotificationLayout / generic
    action_count = 0
    # TimelineItem header (little-endian), matching libpebble2.protocol.timeline
    header = (item_id.bytes + parent_id.bytes +
              struct.pack('<IHBHB', timestamp, duration, TIMELINE_TYPE_NOTIFICATION,
                          flags, layout) +
              struct.pack('<HBB', len(attr_blob), len(attrs), action_count))
    return item_id, header + attr_blob


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--port', type=int, default=12344)
    ap.add_argument('--title', default='Test')
    ap.add_argument('--subtitle', default='')
    ap.add_argument('--body', default='Hello from the injector')
    ap.add_argument('--icon', type=lambda s: int(s, 0), default=DEFAULT_ICON)
    args = ap.parse_args()

    conn = PebbleConnection(QemuTransport(host=args.host, port=args.port))
    conn.connect()
    conn.run_async()
    blobdb = BlobDBClient(conn)

    item_id, value = build_notification(args.title, args.subtitle, args.body, args.icon)
    done = {}

    def cb(status):
        done['status'] = status

    blobdb.insert(BlobDatabaseID.Notification, item_id, value, callback=cb)
    for _ in range(50):
        if 'status' in done:
            break
        time.sleep(0.1)
    print("inject item_id=%s status=%s" % (item_id, done.get('status', 'timeout')))


if __name__ == '__main__':
    main()
