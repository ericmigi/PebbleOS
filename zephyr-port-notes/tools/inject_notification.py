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


def blobdb_insert(item_id, value, token=0x1234, db_id=DB_NOTIFS):
    key = item_id.bytes
    payload = (struct.pack('<BH', BLOBDB_INSERT, token) +
               struct.pack('<B', db_id) +
               struct.pack('<B', len(key)) + key +
               struct.pack('<H', len(value)) + value)
    return payload


DB_PINS = 0x01
TYPE_PIN = 2
DB_APPS = 0x02


def appdb_entry(name, uuid_obj, watchface=False):
    """AppDBEntry (services/blob_db/app_db.h): uuid, info_flags, icon_resource_id,
    app_version, sdk_version, app_face_bg_color, template_id, name[96]."""
    flags = 1 if watchface else 0  # PROCESS_INFO_WATCH_FACE
    return (uuid_obj.bytes + struct.pack('<II', flags, 0) + bytes([1, 0, 5, 86, 0, 0]) +
            name.encode()[:95].ljust(96, b'\0'))
LAYOUT_ALARM = 8
ATTR_ALARM_KIND = 45
ALARM_KIND_JUST_ONCE = 3
# Alarms-data-source parent UUID (timeline.h UUID_ALARMS_DATA_SOURCE).
PIN_PARENT = uuid.UUID(bytes=bytes([0x67, 0xa3, 0x2d, 0x95, 0xef, 0x69, 0x46, 0xd4,
                                    0xa0, 0xb9, 0x85, 0x4c, 0xc6, 0x2f, 0x97, 0xf9]))


def pin_item(title, subtitle, when_ts, kind=ALARM_KIND_JUST_ONCE):
    # Alarm pin: LayoutIdAlarm renders a card; alarm_layout_verify needs only
    # Title + Subtitle. AlarmKind picks the icon (defaults otherwise).
    attrs = [_attr(ATTR_TITLE, title.encode('utf-8')),
             _attr(ATTR_SUBTITLE, subtitle.encode('utf-8')),
             _attr(ATTR_ALARM_KIND, struct.pack('<B', kind))]
    blob = b''.join(attrs)
    item_id = uuid.uuid4()
    # header: id16 + parent16 + <ts u64, dur u16, type u8, flags+status u16, layout u8>
    #         + <payload_len u16, num_attrs u8, num_actions u8>
    # time_t is 8 bytes on the arm-zephyr-eabi target (picolibc 64-bit time_t).
    hdr = (item_id.bytes + PIN_PARENT.bytes +
           struct.pack('<QHBHB', int(when_ts), 0, TYPE_PIN, 0x0001, LAYOUT_ALARM) +
           struct.pack('<HBB', len(blob), len(attrs), 0))
    return item_id, hdr + blob


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
EP_BATTERY = 0x0022
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


def battery_state(percent, charging, plugged):
    # [percent:1][flags:1] flags bit0=charging, bit1=plugged
    flags = (0x01 if charging else 0) | (0x02 if plugged else 0)
    return struct.pack('<BB', max(0, min(100, percent)), flags)


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
    ap.add_argument('--battery', action='store_true',
                    help='set battery state on endpoint 0x0022')
    ap.add_argument('--percent', type=int, default=100, help='battery charge percent')
    ap.add_argument('--charging', action='store_true', help='battery is charging')
    ap.add_argument('--plugged', action='store_true', help='battery is plugged in')
    ap.add_argument('--appdb', action='store_true',
                    help='insert an AppDB entry (endpoint 0xb1db, db 0x02) named --title; --watchface flags it as a face')
    ap.add_argument('--watchface', action='store_true')
    ap.add_argument('--uuid', default=None, help='app uuid for --appdb (default derived from --title)')
    ap.add_argument('--pin', action='store_true',
                    help='insert an alarm timeline pin into the Pins DB (endpoint 0xb1db, db 0x01)')
    ap.add_argument('--when', type=int, default=3600,
                    help='pin time as seconds from now (default +3600; use a future value so it lands in Timeline Future)')
    args = ap.parse_args()

    if args.battery:
        pp = pebble_protocol(EP_BATTERY, battery_state(args.percent, args.charging, args.plugged))
        frame = qemu_frame(PROTO_SPP, pp)
        item_id = 'battery:%d%%' % args.percent
    elif args.appdb:
        u = uuid.UUID(args.uuid) if args.uuid else uuid.uuid5(uuid.NAMESPACE_DNS, args.title)
        pp = pebble_protocol(EP_BLOBDB, blobdb_insert(u, appdb_entry(args.title, u, args.watchface), db_id=DB_APPS))
        frame = qemu_frame(PROTO_SPP, pp)
        item_id = 'appdb:' + args.title
    elif args.pin:
        item_id, value = pin_item(args.title, args.subtitle or 'ONCE', int(time.time()) + args.when)
        pp = pebble_protocol(EP_BLOBDB, blobdb_insert(item_id, value, db_id=DB_PINS))
        frame = qemu_frame(PROTO_SPP, pp)
        item_id = 'pin:' + args.title
    elif args.weather:
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
