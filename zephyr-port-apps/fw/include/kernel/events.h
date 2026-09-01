/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "FreeRTOS.h"
#include "kernel/pebble_tasks.h"
#include "pbl/util/list.h"
#include "pbl/util/uuid.h"
#include "pbl/services/music.h"
#include <pbl/drivers/button_id.h>
#include <bluetooth/bluetooth_types.h>

typedef enum {
  PEBBLE_NULL_EVENT = 0,
  // Values match the shipping kernel/events.h enum (button service compat).
  PEBBLE_BT_CONNECTION_EVENT = 3,
  PEBBLE_BUTTON_DOWN_EVENT = 5,
  PEBBLE_BUTTON_UP_EVENT = 6,
  PEBBLE_BT_PAIRING_EVENT = 12,
  PEBBLE_COMM_SESSION_EVENT = 13,
  PEBBLE_MEDIA_EVENT = 14,
  PEBBLE_TICK_EVENT = 15,
  PEBBLE_SYS_NOTIFICATION_EVENT = 17,
  PEBBLE_ALARM_CLOCK_EVENT = 22,
  PEBBLE_BT_STATE_EVENT = 25,
  PEBBLE_BATTERY_STATE_CHANGE_EVENT = 26,
  PEBBLE_CALLBACK_EVENT = 27,
  PEBBLE_SUBSCRIPTION_EVENT = 29,
  PEBBLE_DO_NOT_DISTURB_EVENT = 32,
  // Subscribed by settings/notifications + quiet_time; charger state never
  // changes in the port (no battery-connection source), needs a distinct slot.
  PEBBLE_BATTERY_CONNECTION_EVENT = 64,
  PEBBLE_BLOBDB_EVENT = 65,
  PEBBLE_BLE_DEVICE_NAME_UPDATED_EVENT = 41,
  // Subscribed to by the real launcher's glance service / glances; never fired
  // in the port (no phone / battery / weather / glance-slice sources yet).
  PEBBLE_WEATHER_EVENT = 60,
  PEBBLE_APP_GLANCE_EVENT = 63,
  // Subscribed to by the settings shared window to refresh on remote pref
  // changes; never fired in the port (prefs are set locally), just needs a
  // distinct enum slot for event_service.
  PEBBLE_PREF_CHANGE_EVENT = 68,
  PEBBLE_NUM_EVENTS = 80,
} PebbleEventType;

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
  Uuid *app_uuid;
} PebbleAppGlanceEvent;

typedef enum {
  NotificationAdded,
  NotificationActedUpon,
  NotificationRemoved,
  NotificationActionResult,
} PebbleSysNotificationType;

typedef struct {
  PebbleSysNotificationType type : 8;
  union {
    Uuid *notification_id;
    void *action_result;
  };
} PebbleSysNotificationEvent;

// Prototype-only shapes for headers the ported apps include (never fired).
typedef struct {
  bool is_active;
} PebbleDoNotDisturbEvent;

typedef struct {
  uint8_t type;
  void *reminder_id;
} PebbleReminderEvent;

typedef struct {
  uint8_t type;
} PebbleHealthEvent;

typedef struct {
  uint8_t type;
} PebbleActivityEvent;

typedef struct {
  uint8_t type;
} PebbleWorkoutEvent;

typedef struct {
  bool is_event_ongoing;
} PebbleCalendarEvent;

typedef struct {
  bool is_open;
  bool is_system;
} PebbleCommSessionEvent;

typedef enum {
  PebbleBluetoothConnectionEventStateConnected,
  PebbleBluetoothConnectionEventStateDisconnected,
} PebbleBluetoothConnectionEventState;

typedef struct {
  PebbleBluetoothConnectionEventState state : 1;
  bool is_ble : 1;
  BTDeviceInternal device;
} PebbleBluetoothConnectionEvent;

typedef struct {
  union {
    PebbleCommSessionEvent comm_session_event;
    PebbleBluetoothConnectionEvent connection;
  };
} PebbleBluetoothEvent;

#include "pbl/services/blob_db/api_types.h"

typedef struct {
  uint8_t db_id;
  BlobDBEventType type;
  uint8_t *key;
  uint8_t key_len;
} PebbleBlobDBEvent;

typedef struct {
  union {
    PebbleTickEvent clock_tick;
    PebbleDoNotDisturbEvent do_not_disturb;
    PebbleBlobDBEvent blob_db;
    PebbleCallbackEvent callback;
    PebbleSubscriptionEvent subscription;
    PebbleButtonEvent button;
    PebbleMediaEvent media;
    PebblePrefChangeEvent pref_change;
    PebbleAppGlanceEvent app_glance;
    PebbleBluetoothEvent bluetooth;
    PebbleSysNotificationEvent sys_notification;
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
