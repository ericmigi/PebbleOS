/* SPDX-License-Identifier: Apache-2.0 */

// Port music now-playing state. Shipping runs services/music (endpoint parse +
// state + album art + generation); the port keeps a small RAM now-playing set
// by the qemu-serial music endpoint (see qemu_notif_rx.c) and answers the few
// music_* queries the Music app uses to pick the now-playing window vs the
// no-music window. Replaces those stubs in app_service_stubs.c.
// ponytail: title/artist/album + a "playing" flag only; no progress, volume,
// album art, or real playback control. Port services/music for those.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pbl/services/music.h"

#define FW_MUSIC_LEN 64

static bool s_has_now_playing;
static char s_title[FW_MUSIC_LEN];
static char s_artist[FW_MUSIC_LEN];
static char s_album[FW_MUSIC_LEN];
static MusicPlayState s_play_state = MusicPlayStatePlaying;
static uint32_t s_pos_ms;
static uint32_t s_len_ms;

static void prv_copy(char *dst, const char *src, size_t src_len) {
  size_t n = src_len < FW_MUSIC_LEN - 1 ? src_len : FW_MUSIC_LEN - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

// Called by the qemu-serial music endpoint on a NowPlayingInfo message.
void fw_music_set_now_playing(const char *title, size_t title_len, const char *artist,
                              size_t artist_len, const char *album, size_t album_len) {
  prv_copy(s_title, title, title_len);
  prv_copy(s_artist, artist, artist_len);
  prv_copy(s_album, album, album_len);
  s_has_now_playing = (s_title[0] || s_artist[0] || s_album[0]);
  s_play_state = MusicPlayStatePlaying;  // a fresh track defaults to playing
  s_pos_ms = 0;
  s_len_ms = 0;  // cleared until a progress update arrives
}

// Called by the music endpoint after a now-playing update (track position +
// length, milliseconds). A non-zero length turns on progress reporting.
void fw_music_set_progress(uint32_t pos_ms, uint32_t len_ms) {
  s_pos_ms = pos_ms;
  s_len_ms = len_ms;
}

// Called by the music endpoint on a PlayStateInfo message. `raw` is the wire
// MusicEndpointPlaybackState (0=paused,1=playing,2=rewinding,3=forwarding).
void fw_music_set_play_state(uint8_t raw) {
  switch (raw) {
    case 0:
      s_play_state = MusicPlayStatePaused;
      break;
    case 1:
      s_play_state = MusicPlayStatePlaying;
      break;
    case 2:
      s_play_state = MusicPlayStateRewinding;
      break;
    case 3:
      s_play_state = MusicPlayStateForwarding;
      break;
    default:
      s_play_state = MusicPlayStateUnknown;
      break;
  }
}

void music_get_now_playing(char *title, char *artist, char *album) {
  if (title) {
    strncpy(title, s_title, FW_MUSIC_LEN);
  }
  if (artist) {
    strncpy(artist, s_artist, FW_MUSIC_LEN);
  }
  if (album) {
    strncpy(album, s_album, FW_MUSIC_LEN);
  }
}

bool music_has_now_playing(void) { return s_has_now_playing; }

bool music_needs_user_to_start_playback_on_phone(void) { return !s_has_now_playing; }

MusicPlayState music_get_playback_state(void) {
  return s_has_now_playing ? s_play_state : MusicPlayStateInvalid;
}

bool music_is_progress_reporting_supported(void) { return s_len_ms > 0; }

void music_get_pos(uint32_t *track_pos_ms, uint32_t *track_length_ms) {
  if (track_pos_ms) {
    *track_pos_ms = s_pos_ms;
  }
  if (track_length_ms) {
    *track_length_ms = s_len_ms;
  }
}

uint32_t music_get_ms_since_pos_last_updated(void) { return 0; }
