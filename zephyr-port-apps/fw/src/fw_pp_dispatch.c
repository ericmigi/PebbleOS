/* SPDX-License-Identifier: Apache-2.0 */

// Pebble Protocol endpoint dispatch for the port, shared by every transport:
// the qemu serial framing (qemu_notif_rx.c) and PPoGATT over BLE (ppog_min.c)
// both hand de-framed PP messages here. Routes BlobDB notification / pin
// inserts into the notification display + pin_db, and the port-custom music /
// weather / battery endpoints into their glue state, then acks BlobDB commands
// so the phone's sync keeps flowing.

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <stdbool.h>
#include <string.h>

#include "kernel/events.h"

#define BLOB_DB_ENDPOINT 0xb1db
#define MUSIC_ENDPOINT 0x0020
#define MUSIC_CMD_NOW_PLAYING 0x10
#define MUSIC_CMD_PLAY_STATE 0x11

extern void fw_music_set_now_playing(const char *title, size_t title_len, const char *artist,
                                     size_t artist_len, const char *album, size_t album_len);
extern void fw_music_set_play_state(uint8_t raw);
extern void fw_music_set_progress(uint32_t pos_ms, uint32_t len_ms);

static uint32_t prv_rd32le(const uint8_t *p);

// Live-refresh events: the music app + launcher glances subscribe to
// PEBBLE_MEDIA_EVENT / PEBBLE_WEATHER_EVENT, so emitting them after an inject
// makes an already-open screen update instead of only refreshing on re-render.
extern void event_put(PebbleEvent *event);
extern MusicPlayState music_get_playback_state(void);

static void prv_emit_media(PebbleMediaEventType type) {
  PebbleEvent e = {.type = PEBBLE_MEDIA_EVENT};
  e.media.type = type;
  if (type == PebbleMediaEventTypePlaybackStateChanged) {
    e.media.playback_state = music_get_playback_state();
  }
  event_put(&e);
}

static void prv_emit_weather(void) {
  PebbleEvent e = {.type = PEBBLE_WEATHER_EVENT};
  event_put(&e);
}

// Port-custom battery endpoint. Payload: [percent:1][flags:1] where flags bit0
// = charging, bit1 = plugged. The Settings glance re-peeks on the change event.
#define BATTERY_ENDPOINT 0x0022
extern void fw_battery_set(uint8_t percent, bool charging, bool plugged);

static void prv_handle_battery(const uint8_t *data, uint16_t pp_len) {
  if (pp_len < 2) {
    return;
  }
  const uint8_t flags = data[1];
  fw_battery_set(data[0], flags & 0x01, flags & 0x02);
  PebbleEvent e = {.type = PEBBLE_BATTERY_STATE_CHANGE_EVENT};
  event_put(&e);
}

// Port-custom weather endpoint (no phone on the qemu shell). Payload:
// [loc_len:1][loc][temp:2 LE signed][type:1][phrase_len:1][phrase].
#define WEATHER_ENDPOINT 0x0021
extern void fw_weather_set(const char *location, size_t loc_len, int temp, int type,
                           const char *phrase, size_t phrase_len);

static void prv_handle_weather(const uint8_t *data, uint16_t pp_len) {
  const uint8_t *iter = data;
  const uint8_t *end = data + pp_len;
  if (iter >= end) {
    return;
  }
  uint8_t loc_len = *iter++;
  if (iter + loc_len + 3 > end) {
    return;
  }
  const char *loc = (const char *)iter;
  iter += loc_len;
  int16_t temp = (int16_t)(iter[0] | (iter[1] << 8));
  iter += 2;
  uint8_t type = *iter++;
  if (iter >= end) {
    return;
  }
  uint8_t phrase_len = *iter++;
  if (iter + phrase_len > end) {
    return;
  }
  fw_weather_set(loc, loc_len, temp, type, (const char *)iter, phrase_len);
  prv_emit_weather();
}

// Music now-playing on endpoint 0x0020: [cmd:1][artist][album][title], each
// string a 1-byte length prefix + bytes (mirrors services/music/endpoint.c).
static void prv_handle_music(const uint8_t *data, uint16_t pp_len) {
  if (pp_len < 1) {
    return;
  }
  if (data[0] == MUSIC_CMD_PLAY_STATE) {
    // PlayStateInfo: [cmd][play_state:1][track_pos:4][rate:4]...; only the
    // play_state byte is used here.
    if (pp_len >= 2) {
      fw_music_set_play_state(data[1]);
      prv_emit_media(PebbleMediaEventTypePlaybackStateChanged);
    }
    return;
  }
  if (data[0] != MUSIC_CMD_NOW_PLAYING) {
    return;
  }
  const uint8_t *iter = data + 1;
  const uint8_t *end = data + pp_len;
  const char *strs[3] = {"", "", ""};
  size_t lens[3] = {0, 0, 0};
  for (int i = 0; i < 3; i++) {
    if (iter >= end) {
      return;
    }
    const uint8_t slen = *iter++;
    if (iter + slen > end) {
      return;
    }
    strs[i] = (const char *)iter;
    lens[i] = slen;
    iter += slen;
  }
  // Wire order is artist, album, title.
  fw_music_set_now_playing(strs[2], lens[2], strs[0], lens[0], strs[1], lens[1]);
  // Optional trailing progress: pos_ms(4 LE) + len_ms(4 LE).
  if (iter + 8 <= end) {
    fw_music_set_progress(prv_rd32le(iter), prv_rd32le(iter + 4));
  } else {
    fw_music_set_progress(0, 0);
  }
  prv_emit_media(PebbleMediaEventTypeNowPlayingChanged);
  prv_emit_media(PebbleMediaEventTypeTrackPosChanged);
}
#define BLOB_DB_CMD_INSERT 0x01
#define BLOB_DB_CMD_INSERT_TS 0x0D
#define BLOB_DB_ID_NOTIFS 0x04
#define BLOB_DB_ID_PINS 0x01
#define BLOB_DB_ID_APPS 0x02
#define BLOB_DB_CMD_DELETE 0x04
#define BLOB_DB_CMD_CLEAR 0x05
#define BLOB_DB_ACK_GENERAL_FAILURE 0x02
#define APP_FETCH_ENDPOINT 0x1771
#define APP_RUN_STATE_ENDPOINT 0x0034

// fw_pbw_install.c
bool fw_pbw_appdb_insert(const uint8_t *key, int key_len, const uint8_t *val, int val_len);
bool fw_pbw_appdb_delete(const uint8_t *key, int key_len);
bool fw_pbw_appdb_clear(void);
void fw_pbw_handle_fetch_response(const uint8_t *data, uint16_t len);
void fw_pbw_launch_uuid(const uint8_t uuid[16]);
#define BLOB_DB_ACK_SUCCESS 0x01
#define BLOB_DB_ACK_DB_NOT_SUPPORTED 0x09

// pin_db stores the raw serialized TimelineItem keyed by its UUID; Timeline
// Future/Past re-read pin_db on open, so no event emission is needed here.
extern int32_t pin_db_insert(const uint8_t *key, int key_len, const uint8_t *val, int val_len);

#define ATTR_TITLE 1
#define ATTR_SUBTITLE 2
#define ATTR_BODY 3
#define ATTR_ICON 4

void fw_notification_show(const char *title, const char *subtitle, const char *body,
                          uint32_t icon);
__attribute__((weak)) void fw_notification_show(const char *title, const char *subtitle,
                                                const char *body, uint32_t icon) {
  (void)icon;
  printk("NOTIF_RX title=\"%s\" subtitle=\"%s\" body=\"%s\"\n",
         title ? title : "", subtitle ? subtitle : "", body ? body : "");
}

static uint16_t prv_rd16be(const uint8_t *p) { return ((uint16_t)p[0] << 8) | p[1]; }
static uint16_t prv_rd16le(const uint8_t *p) { return ((uint16_t)p[1] << 8) | p[0]; }
static uint32_t prv_rd32le(const uint8_t *p) {
  return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
}

// Parse a raw Pebble Protocol payload (already de-framed): [len:2 BE][ep:2 BE][data].
// Ack for a BlobDB command: [token:2 LE][result:1] back on the BlobDB endpoint.
// Sent through fw_pp_send, which the BLE transport (ppog_min.c) provides; the
// qemu serial path has no phone waiting on it, so its default is a no-op.
__attribute__((weak)) bool fw_pp_send(uint16_t endpoint, const uint8_t *payload, uint16_t len) {
  (void)endpoint; (void)payload; (void)len;
  return false;
}

static void prv_blob_db_ack(const uint8_t *data, uint8_t result) {
  const uint8_t ack[3] = { data[1], data[2], result };  // token echoed LE, then result
  (void)fw_pp_send(BLOB_DB_ENDPOINT, ack, sizeof(ack));
}

// Pebble Protocol payload for one endpoint, from any transport (qemu serial
// framing in qemu_notif_rx.c, PPoGATT over BLE in ppog_min.c).
void fw_pp_handle_endpoint(uint16_t endpoint, const uint8_t *data, uint16_t pp_len) {
  if (endpoint == MUSIC_ENDPOINT) {
    prv_handle_music(data, pp_len);
    return;
  }
  if (endpoint == WEATHER_ENDPOINT) {
    prv_handle_weather(data, pp_len);
    return;
  }
  if (endpoint == BATTERY_ENDPOINT) {
    prv_handle_battery(data, pp_len);
    return;
  }
  if (endpoint == APP_FETCH_ENDPOINT) {
    fw_pbw_handle_fetch_response(data, pp_len);
    return;
  }
  if (endpoint == APP_RUN_STATE_ENDPOINT) {
    // [cmd][uuid]: 1 run, 2 stop (ignored), 3 status (answered by the transport)
    if (pp_len >= 17 && data[0] == 0x01) {
      fw_pbw_launch_uuid(data + 1);
    }
    return;
  }
  if (endpoint != BLOB_DB_ENDPOINT) {
    return;
  }
  // BlobDB: [cmd:1][token:2][db_id:1][key_len:1][key:N][val_len:2][value:M]
  if (pp_len < 4) {
    return;
  }
  const uint8_t cmd = data[0];
  const uint8_t db_id = data[3];
  printk("BLOBDB cmd=0x%02x db=%u len=%u\n", cmd, db_id, pp_len);
  if (db_id == BLOB_DB_ID_APPS) {
    bool ok = false;
    if (cmd == BLOB_DB_CMD_CLEAR) {
      ok = fw_pbw_appdb_clear();
    } else if (pp_len >= 5 && (cmd == BLOB_DB_CMD_DELETE || cmd == BLOB_DB_CMD_INSERT ||
                               cmd == BLOB_DB_CMD_INSERT_TS)) {
      const uint8_t key_len = data[4];
      const uint8_t *key = data + 5;
      if (5 + key_len > pp_len) {
        return;
      }
      if (cmd == BLOB_DB_CMD_DELETE) {
        ok = fw_pbw_appdb_delete(key, key_len);
      } else if (5 + key_len + 2 <= pp_len) {
        const uint16_t val_len = prv_rd16le(key + key_len);
        const uint8_t *val = key + key_len + 2;
        if (val + val_len <= data + pp_len) {
          ok = fw_pbw_appdb_insert(key, key_len, val, val_len);
        }
      }
    } else {
      ok = true;  // read/update: not implemented in shipping either
    }
    prv_blob_db_ack(data, ok ? BLOB_DB_ACK_SUCCESS : BLOB_DB_ACK_GENERAL_FAILURE);
    return;
  }
  if (pp_len < 7) {
    return;
  }
  if (cmd != BLOB_DB_CMD_INSERT && cmd != BLOB_DB_CMD_INSERT_TS) {
    // Deletes/clears (phone-side dismissals, DB resets) are accepted so the
    // phone's sync state machine keeps moving; the watch keeps its own copy.
    prv_blob_db_ack(data, BLOB_DB_ACK_SUCCESS);
    return;
  }
  if (db_id != BLOB_DB_ID_NOTIFS && db_id != BLOB_DB_ID_PINS) {
    prv_blob_db_ack(data, BLOB_DB_ACK_DB_NOT_SUPPORTED);
    return;
  }
  const uint8_t key_len = data[4];
  const uint8_t *iter = data + 5 + key_len;
  const uint8_t *end = data + pp_len;
  if (iter + 2 > end) {
    return;
  }
  const uint16_t val_len = prv_rd16le(iter);
  iter += 2;
  const uint8_t *value = iter;
  if (value + val_len > end) {
    return;
  }
  // Pins: store the raw serialized item straight into pin_db (Timeline
  // Future/Past render it as a real layout card). Notifs fall through to the
  // decode-and-show path below.
  if (db_id == BLOB_DB_ID_PINS) {
    pin_db_insert(data + 5, key_len, value, val_len);
    prv_blob_db_ack(data, BLOB_DB_ACK_SUCCESS);
    return;
  }
  // TimelineItem (LE): uuid16 + parent16 + ts4 + dur2 + type1 + flags2 + layout1
  //                    + data_len2 + attr_count1 + action_count1 + attributes...
  const size_t hdr = 16 + 16 + 4 + 2 + 1 + 2 + 1;
  if (val_len < hdr + 4) {
    return;
  }
  const uint8_t attr_count = value[hdr + 2];
  const uint8_t *a = value + hdr + 4;
  const uint8_t *vend = value + val_len;
  static char title[64], subtitle[64], body[128];
  title[0] = subtitle[0] = body[0] = '\0';
  uint32_t icon = 0;
  for (uint8_t i = 0; i < attr_count && a + 3 <= vend; ++i) {
    const uint8_t id = a[0];
    const uint16_t alen = prv_rd16le(a + 1);
    const uint8_t *content = a + 3;
    if (content + alen > vend) {
      break;
    }
    char *dst = NULL;
    size_t cap = 0;
    if (id == ATTR_TITLE) { dst = title; cap = sizeof(title); }
    else if (id == ATTR_SUBTITLE) { dst = subtitle; cap = sizeof(subtitle); }
    else if (id == ATTR_BODY) { dst = body; cap = sizeof(body); }
    else if (id == ATTR_ICON && alen == 4) { icon = prv_rd32le(content); }
    if (dst) {
      const size_t n = (alen < cap - 1) ? alen : cap - 1;
      memcpy(dst, content, n);
      dst[n] = '\0';
    }
    a = content + alen;
  }
  fw_notification_show(title, subtitle, body, icon);
  prv_blob_db_ack(data, BLOB_DB_ACK_SUCCESS);
}

// Raw Pebble Protocol message: [len:2 BE][endpoint:2 BE][payload].
void fw_pp_handle(const uint8_t *msg, uint16_t len) {
  if (len < 4) {
    return;
  }
  const uint16_t pp_len = prv_rd16be(msg);
  const uint16_t endpoint = prv_rd16be(msg + 2);
  if (4 + pp_len > len) {
    return;
  }
  fw_pp_handle_endpoint(endpoint, msg + 4, pp_len);
}

