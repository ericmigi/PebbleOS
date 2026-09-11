/* SPDX-License-Identifier: Apache-2.0 */

// Exported-table entries third-party PBWs call that the port's applib subset
// did not provide: the SDK's by-value ABI wrappers, the pbl_std time/locale
// shims, and the phone-connection / battery subscription services.

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <zephyr/kernel.h>

extern size_t strftime(char *s, size_t max, const char *fmt, const struct tm *tm);
extern time_t mktime(struct tm *tb);

#include "applib/battery_state_service.h"
#include "applib/connection_service.h"
#include "applib/graphics/graphics.h"
#include "applib/ui/layer.h"
#include "applib/ui/menu_layer.h"
#include "applib/ui/window.h"

// --- by-value ABI wrappers (the SDK passes structs by value across the table)
void window_set_window_handlers_by_value(Window *window, WindowHandlers handlers) {
  window_set_window_handlers(window, &handlers);
}

// --- pbl_std: apps go through these instead of libc so shipping can apply
// the user's timezone/locale. The port keeps wall time in the RTC already.
extern time_t rtc_get_time(void);
time_t pbl_override_time(time_t *tloc) {
  const time_t now = rtc_get_time();
  if (tloc) {
    *tloc = now;
  }
  return now;
}
time_t pbl_override_time_legacy(time_t *tloc) { return pbl_override_time(tloc); }
struct tm *pbl_override_localtime(const time_t *timep) { return localtime(timep); }
struct tm *pbl_override_gmtime(const time_t *timep) { return gmtime(timep); }
time_t pbl_override_mktime(struct tm *tb) { return mktime(tb); }
double pbl_override_difftime(time_t end, time_t beginning) { return (double)(end - beginning); }
uint16_t pbl_override_time_ms_legacy(time_t *t_loc, uint16_t *out_ms) {
  const time_t now = pbl_override_time(t_loc);
  const uint16_t ms = (uint16_t)(k_uptime_get() % 1000);
  if (out_ms) {
    *out_ms = ms;
  }
  return (uint16_t)now;
}
size_t pbl_strftime(char *s, size_t maxsize, const char *format, const struct tm *tm_p) {
  return strftime(s, maxsize, format, tm_p);
}
int pbl_snprintf(char *str, size_t n, const char *format, ...) {
  va_list args;
  va_start(args, format);
  const int rc = vsnprintf(str, n, format, args);
  va_end(args);
  return rc;
}
void *pbl_memcpy(void *destination, const void *source, size_t num) {
  return memcpy(destination, source, num);
}
char *pbl_setlocale(int category, const char *locale) {
  (void)category; (void)locale;
  return "en_US";
}

// --- phone connection + battery subscriptions
// ponytail: handlers are stored and fired from the port's BLE/battery events
// when those land; peek reflects the PPoGATT session.
extern bool fw_pp_session_is_up(void) __attribute__((weak));
static ConnectionHandlers s_conn_handlers;
static BatteryStateHandler s_battery_handler;
extern BatteryChargeState battery_state_service_peek(void);

bool connection_service_peek_pebble_app_connection(void) {
  return fw_pp_session_is_up ? fw_pp_session_is_up() : false;
}
bool connection_service_peek_pebblekit_connection(void) { return false; }
void connection_service_subscribe(ConnectionHandlers conn_handlers) { s_conn_handlers = conn_handlers; }
void connection_service_unsubscribe(void) { s_conn_handlers = (ConnectionHandlers){0}; }
void bluetooth_connection_service_subscribe(ConnectionHandler handler) {
  s_conn_handlers.pebble_app_connection_handler = handler;
}
void bluetooth_connection_service_unsubscribe(void) { s_conn_handlers = (ConnectionHandlers){0}; }
void battery_state_service_subscribe(BatteryStateHandler handler) { s_battery_handler = handler; }
void battery_state_service_unsubscribe(void) { s_battery_handler = NULL; }

// --- persist: applib/persist.c minus the syscall plumbing, over the real
// persist service (settings file per app uuid, opened around the PBW run).
#include "pbl/services/persist.h"
#include "pbl/services/settings/settings_file.h"
#include "pbl/util/uuid.h"
#include "system/status_codes.h"

extern const Uuid *fw_pbw_current_uuid(void);
#define PERSIST_DATA_MAX_LENGTH_PORT 256

static SettingsFile *prv_store(void) {
  const Uuid *uuid = fw_pbw_current_uuid();
  return uuid ? persist_service_lock_and_get_store(uuid) : NULL;
}
static void prv_unstore(SettingsFile *store) {
  if (store) {
    persist_service_unlock_store(store);
  }
}

size_t persist_get_max_size(void) { return persist_service_get_max_size(); }
bool persist_exists(const uint32_t key) {
  SettingsFile *store = prv_store();
  const bool r = store && settings_file_exists(store, &key, sizeof(key));
  prv_unstore(store);
  return r;
}
int persist_get_size(const uint32_t key) {
  SettingsFile *store = prv_store();
  const int len = store ? settings_file_get_len(store, &key, sizeof(key)) : 0;
  prv_unstore(store);
  return len ? len : E_DOES_NOT_EXIST;
}
int persist_read_data(const uint32_t key, void *buffer, const size_t buffer_size) {
  SettingsFile *store = prv_store();
  if (!store) {
    return E_DOES_NOT_EXIST;
  }
  const int len = settings_file_get_len(store, &key, sizeof(key));
  if (len == 0) {
    prv_unstore(store);
    return E_DOES_NOT_EXIST;
  }
  const size_t n = (size_t)len < buffer_size ? (size_t)len : buffer_size;
  const status_t rc = settings_file_get(store, &key, sizeof(key), buffer, n);
  prv_unstore(store);
  return PASSED(rc) ? (int)n : rc;
}
bool persist_read_bool(const uint32_t key) {
  bool v = false;
  (void)persist_read_data(key, &v, sizeof(v));
  return v;
}
int32_t persist_read_int(const uint32_t key) {
  int32_t v = 0;
  (void)persist_read_data(key, &v, sizeof(v));
  return v;
}
int persist_read_string(const uint32_t key, char *buffer, const size_t buffer_size) {
  const int rc = persist_read_data(key, buffer, buffer_size);
  if (rc > 0 && buffer_size) {
    buffer[(size_t)rc < buffer_size ? (size_t)rc : buffer_size - 1] = '\0';
  }
  return rc;
}
int persist_write_data(const uint32_t key, const void *data, const size_t size) {
  SettingsFile *store = prv_store();
  if (!store) {
    return E_DOES_NOT_EXIST;
  }
  const size_t n = size < PERSIST_DATA_MAX_LENGTH_PORT ? size : PERSIST_DATA_MAX_LENGTH_PORT;
  const status_t rc = settings_file_set(store, &key, sizeof(key), data, n);
  prv_unstore(store);
  return PASSED(rc) ? (int)n : rc;
}
status_t persist_write_bool(const uint32_t key, const bool value) {
  return persist_write_data(key, &value, sizeof(value));
}
status_t persist_write_int(const uint32_t key, const int32_t value) {
  return persist_write_data(key, &value, sizeof(value));
}
int persist_write_string(const uint32_t key, const char *cstring) {
  return persist_write_data(key, cstring, strlen(cstring) + 1);
}
status_t persist_delete(const uint32_t key) {
  SettingsFile *store = prv_store();
  if (!store) {
    return E_DOES_NOT_EXIST;
  }
  status_t rc = E_DOES_NOT_EXIST;
  if (settings_file_exists(store, &key, sizeof(key))) {
    rc = settings_file_delete(store, &key, sizeof(key));
  }
  prv_unstore(store);
  return rc;
}
