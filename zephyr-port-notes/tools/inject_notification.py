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


EP_MUSIC = 0x0020
EP_WEATHER = 0x0021
MUSIC_CMD_NOW_PLAYING = 0x10
MUSIC_CMD_PLAY_STATE = 0x11


def _mstr(text):
    b = text.encode('utf-8')[:255]
    return struct.pack('<B', len(b)) + b


def music_now_playing(artist, album, title, pos_ms=0, len_ms=0):
    # cmd | artist | album | title | [pos_ms(4 LE) | len_ms(4 LE)] (port extension)
    msg = struct.pack('<B', MUSIC_CMD_NOW_PLAYING) + _mstr(artist) + _mstr(album) + _mstr(title)
    if len_ms:
        msg += struct.pack('<II', pos_ms, len_ms)
    return msg


_MUSIC_STATE = {'paused': 0, 'playing': 1, 'rewinding': 2, 'forwarding': 3}


def music_play_state(state):
    # PlayStateInfo: cmd | play_state(1) | track_pos_ms(4 LE) | play_rate(4 LE) |
    # shuffle(1) | repeat(1)
    return struct.pack('<BBiiBB', MUSIC_CMD_PLAY_STATE, _MUSIC_STATE.get(state, 1),
                       -1, 100, 0, 0)


def weather_now(location, temp, wtype, phrase):
    # [loc_len][loc][temp:2 LE signed][type:1][phrase_len][phrase]
    loc = location.encode('utf-8')[:255]
    ph = phrase.encode('utf-8')[:255]
    return (struct.pack('<B', len(loc)) + loc + struct.pack('<hB', temp, wtype) +
            struct.pack('<B', len(ph)) + ph)


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
    ap.add_argument('--sock', default=None, help='connect to a unix-domain serial socket instead of tcp host:port')
    ap.add_argument('--music', action='store_true',
                    help='send a now-playing update on the music endpoint (0x0020)')
    ap.add_argument('--artist', default='Some Artist')
    ap.add_argument('--music-state', dest='music_state', choices=['playing','paused','rewinding','forwarding'], default=None, help='send a music play-state update (endpoint 0x0020 cmd 0x11)')
    ap.add_argument('--album', default='Some Album')
    ap.add_argument('--weather', action='store_true', help='send a weather forecast on endpoint 0x0021')
    ap.add_argument('--location', default='San Francisco')
    ap.add_argument('--temp', type=int, default=68)
    ap.add_argument('--wtype', type=int, default=7, help='WeatherType numeric id (7=Sun)')
    ap.add_argument('--phrase', default='Sunny')
    ap.add_argument('--pos', type=int, default=0, help='music track position ms')
    ap.add_argument('--length', type=int, default=0, help='music track length ms (enables progress bar)')
    ap.add_argument('--ancs', action='store_true',
                    help='send a real ANCS attribute-response over PROTO_ANCS instead of a BlobDB insert')
    args = ap.parse_args()

    if args.weather:
        pp = pebble_protocol(EP_WEATHER, weather_now(args.location, args.temp, args.wtype, args.phrase))
        frame = qemu_frame(PROTO_SPP, pp)
        item_id = 'weather:' + args.location
    elif args.music_state:
        pp = pebble_protocol(EP_MUSIC, music_play_state(args.music_state))
        frame = qemu_frame(PROTO_SPP, pp)
        item_id = 'music-state:' + args.music_state
    elif args.music:
        pp = pebble_protocol(EP_MUSIC, music_now_playing(args.artist, args.album, args.title, args.pos, args.length))
        frame = qemu_frame(PROTO_SPP, pp)
        item_id = 'music:' + args.title
    elif args.ancs:
        data = ancs_notif_attr_response(args.title, args.subtitle, args.body)
        frame = qemu_frame(PROTO_ANCS, data)
        item_id = 'ancs'
    else:
        item_id, value = timeline_item(args.title, args.subtitle, args.body, args.icon)
        pp = pebble_protocol(EP_BLOBDB, blobdb_insert(item_id, value))
        frame = qemu_frame(PROTO_SPP, pp)

    if args.sock:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(args.sock)
    else:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((args.host, args.port))
    s.sendall(frame)
    time.sleep(0.3)
    s.close()
    print("injected item_id=%s bytes=%d" % (item_id, len(frame)))


if __name__ == '__main__':
    main()
