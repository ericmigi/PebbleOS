/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "FreeRTOS.h"
#include "kernel/pebble_tasks.h"
#include "pbl/util/list.h"
#include <pbl/drivers/button_id.h>

#include "pbl/services/music.h"

typedef enum {
  PEBBLE_NULL_EVENT = 0,
  // Values match the shipping kernel/events.h enum (button service compat).
  PEBBLE_BUTTON_DOWN_EVENT = 5,
  PEBBLE_BUTTON_UP_EVENT = 6,
  PEBBLE_MEDIA_EVENT = 14,
  PEBBLE_TICK_EVENT = 15,
  PEBBLE_CALLBACK_EVENT = 27,
  PEBBLE_SUBSCRIPTION_EVENT = 29,
  PEBBLE_PREF_CHANGE_EVENT = 68,
  PEBBLE_NUM_EVENTS = 80,
} PebbleEventType;

// Media (music now-playing) event, mirrored from shipping kernel/events.h. The
// Music app subscribes to this; the port music service stub never posts one, so
// the app renders its initial (no-music) state.
typedef enum {
  PebbleMediaEventTypeNowPlayingChanged,
  PebbleMediaEventTypePlaybackStateChanged,
  PebbleMediaEventTypeVolumeChanged,
  PebbleMediaEventTypeServerConnected,
  PebbleMediaEventTypeServerDisconnected,
  PebbleMediaEventTypeTrackPosChanged,
  PebbleMediaEventTypeAlbumArtUpdated,
} PebbleMediaEventType;

typedef struct {
  PebbleMediaEventType type;
  union {
    MusicPlayState playback_state;
    uint8_t volume_percent;
  };
} PebbleMediaEvent;

typedef struct {
  const char *key;
  uint8_t key_len;
} PebblePrefChangeEvent;

typedef void (*CallbackEventCallback)(void *data);

typedef struct {
  time_t tick_time;
} PebbleTickEvent;

typedef struct {
  CallbackEventCallback callback;
  void *data;
} PebbleCallbackEvent;

typedef struct {
  ButtonId button_id;
} PebbleButtonEvent;

typedef struct {
  bool subscribe;
  PebbleTask task : 8;
  PebbleEventType event_type;
  void *event_queue;
} PebbleSubscriptionEvent;

typedef struct {
  union {
    PebbleTickEvent clock_tick;
    PebbleCallbackEvent callback;
    PebbleSubscriptionEvent subscription;
    PebbleButtonEvent button;
    PebbleMediaEvent media;
    PebblePrefChangeEvent pref_change;
  };
  PebbleTaskBitset task_mask;
  PebbleEventType type : 8;
} PebbleEvent;

void events_init(void);
void event_put(PebbleEvent *event);
bool event_put_isr(PebbleEvent *event);
void event_put_from_process(PebbleTask task, PebbleEvent *event);
bool event_try_put_from_process(PebbleTask task, PebbleEvent *event);
bool event_take_timeout(PebbleEvent *event, int timeout_ms);
void **event_get_buffer(PebbleEvent *event);
void event_deinit(PebbleEvent *event);
void event_cleanup(PebbleEvent *event);
void event_reset_from_process_queue(PebbleTask task);
QueueHandle_t event_get_to_kernel_queue(PebbleTask task);
QueueHandle_t event_kernel_to_kernel_event_queue(void);
BaseType_t event_queue_cleanup_and_reset(QueueHandle_t queue);
