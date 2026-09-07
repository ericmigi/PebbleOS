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
  return s_has_now_playing ? MusicPlayStatePlaying : MusicPlayStateInvalid;
}
