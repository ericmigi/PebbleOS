#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0
"""Inject a fake notification into a running Pebble QEMU (FreeRTOS reference or
Zephyr port) by writing a BlobDB-insert into the Notifications DB straight onto
the pebble-tool serial socket — the same path CoreApp/Android uses, minus the
watch-info handshake so it works even before a comm session is up.

  python3 inject_notification.py --port 12344 --title Alice --body "Dinner at 7?"

Plain python3 (stdlib only). QEMU frame: FEED | proto(SPP=1) | len | data | BEEF.
Data = Pebble Protocol: len | endpoint(0xb1db BlobDB) | [cmd|token|dbid|keylen|key|vallen|value].
Value = serialized TimelineItem (notification).
"""
import argparse
import os
import socket
import struct
import time
import uuid

QEMU_HDR = 0xFEED
QEMU_FTR = 0xBEEF
PROTO_SPP = 1
PROTO_ANCS = 0xf001
# ANCS NotificationAttributeID
ANCS_ATTR_APPID, ANCS_ATTR_TITLE, ANCS_ATTR_SUBTITLE = 0, 1, 2
ANCS_ATTR_MESSAGE, ANCS_ATTR_DATE = 3, 5
ANCS_CMD_GET_NOTIF_ATTRS = 0
EP_BLOBDB = 0xb1db
BLOBDB_INSERT = 0x01
DB_NOTIFS = 0x04
TYPE_NOTIFICATION = 1
ATTR_TITLE, ATTR_SUBTITLE, ATTR_BODY, ATTR_ICON = 1, 2, 3, 4
DEFAULT_ICON = 0x80000000 | 4


def _attr(aid, content):
    return struct.pack('<BH', aid, len(content)) + content


def timeline_item(title, subtitle, body, icon):
    attrs = []
    if title:
        attrs.append(_attr(ATTR_TITLE, title.encode('utf-8')))
    if subtitle:
        attrs.append(_attr(ATTR_SUBTITLE, subtitle.encode('utf-8')))
    if body:
        attrs.append(_attr(ATTR_BODY, body.encode('utf-8')))
    attrs.append(_attr(ATTR_ICON, struct.pack('<I', icon)))
    blob = b''.join(attrs)
    item_id = uuid.uuid4()
    parent = uuid.UUID(int=0)
    hdr = (item_id.bytes + parent.bytes +
           struct.pack('<IHBHB', int(time.time()), 0, TYPE_NOTIFICATION, 0, 0x01) +
           struct.pack('<HBB', len(blob), len(attrs), 0))
    return item_id, hdr + blob


def blobdb_insert(item_id, value, token=0x1234):
    key = item_id.bytes
    payload = (struct.pack('<BH', BLOBDB_INSERT, token) +
               struct.pack('<B', DB_NOTIFS) +
               struct.pack('<B', len(key)) + key +
               struct.pack('<H', len(value)) + value)
    return payload


def _ancs_attr(aid, content):
    # ANCS Data Source attribute: id | length(2 LE) | value (not null-terminated)
    return struct.pack('<BH', aid, len(content)) + content


def ancs_notif_attr_response(title, subtitle, body, uid=1,
                             app_id='com.apple.MobileSMS',
                             date='20260903T081300'):
    """Build the exact bytes an iPhone's ANCS Data Source characteristic sends in
    response to a Get-Notification-Attributes command:
      command_id(1) | notification_uid(4 LE) | [attr_id | len(2 LE) | value] ...
    This is what the on-device GATT layer would hand to fw_ancs_feed."""
    attrs = _ancs_attr(ANCS_ATTR_APPID, app_id.encode('utf-8'))
    if title:
        attrs += _ancs_attr(ANCS_ATTR_TITLE, title.encode('utf-8'))
    if subtitle:
        attrs += _ancs_attr(ANCS_ATTR_SUBTITLE, subtitle.encode('utf-8'))
    if body:
        attrs += _ancs_attr(ANCS_ATTR_MESSAGE, body.encode('utf-8'))
    attrs += _ancs_attr(ANCS_ATTR_DATE, date.encode('utf-8'))
    header = struct.pack('<BI', ANCS_CMD_GET_NOTIF_ATTRS, uid)
    return header + attrs


def pebble_protocol(endpoint, payload):
    return struct.pack('>HH', len(payload), endpoint) + payload


def qemu_frame(proto, data):
    return struct.pack('>HHH', QEMU_HDR, proto, len(data)) + data + struct.pack('>H', QEMU_FTR)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--port', type=int, default=12344)
    ap.add_argument('--title', default='Test')
    ap.add_argument('--subtitle', default='')
    ap.add_argument('--body', default='Hello from the injector')
    ap.add_argument('--icon', type=lambda s: int(s, 0), default=DEFAULT_ICON)
    ap.add_argument('--ancs', action='store_true',
                    help='send a real ANCS attribute-response over PROTO_ANCS instead of a BlobDB insert')
    args = ap.parse_args()

    if args.ancs:
        data = ancs_notif_attr_response(args.title, args.subtitle, args.body)
        frame = qemu_frame(PROTO_ANCS, data)
        item_id = 'ancs'
    else:
        item_id, value = timeline_item(args.title, args.subtitle, args.body, args.icon)
        pp = pebble_protocol(EP_BLOBDB, blobdb_insert(item_id, value))
        frame = qemu_frame(PROTO_SPP, pp)

    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((args.host, args.port))
    s.sendall(frame)
    time.sleep(0.3)
    s.close()
    print("injected item_id=%s bytes=%d" % (item_id, len(frame)))


if __name__ == '__main__':
    main()
