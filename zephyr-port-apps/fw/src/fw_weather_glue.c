/* SPDX-License-Identifier: Apache-2.0 */

// Port weather state. Shipping stores forecasts in blob_db weather_db and the
// weather_service builds a WeatherLocationForecast from it; the port keeps a
// small RAM forecast set by the qemu-serial weather endpoint (qemu_notif_rx.c)
// and hands it to the launcher Weather glance via
// weather_service_create_default_forecast (moved here from shell_glue.c).
// ponytail: single default forecast, current conditions only (no hourly/daily
// arrays, no persistence). Port services/weather + weather_db for the full set.

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "pbl/services/weather/weather_service.h"
#include "kernel/pbl_malloc.h"

#define FW_WX_LEN 48

static bool s_has;
static char s_location[FW_WX_LEN];
static char s_phrase[FW_WX_LEN];
static int s_temp;
static int s_type = WeatherType_Generic;

static void prv_copy(char *dst, const char *src, size_t src_len) {
  size_t n = src_len < FW_WX_LEN - 1 ? src_len : FW_WX_LEN - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

void fw_weather_set(const char *location, size_t loc_len, int temp, int type, const char *phrase,
                    size_t phrase_len) {
  prv_copy(s_location, location, loc_len);
  prv_copy(s_phrase, phrase, phrase_len);
  s_temp = temp;
  s_type = type;
  s_has = true;
}

WeatherLocationForecast *weather_service_create_default_forecast(void) {
  if (!s_has) {
    return NULL;
  }
  WeatherLocationForecast *f = kernel_zalloc(sizeof(*f));
  if (!f) {
    return NULL;
  }
  f->location_name = kernel_strdup(s_location);
  f->current_weather_phrase = kernel_strdup(s_phrase);
  f->is_current_location = true;
  f->current_temp = s_temp;
  f->today_high = s_temp;
  f->today_low = s_temp;
  f->current_weather_type = (WeatherType)s_type;
  f->tomorrow_high = s_temp;
  f->tomorrow_low = s_temp;
  f->tomorrow_weather_type = (WeatherType)s_type;
  return f;
}

void weather_service_destroy_default_forecast(WeatherLocationForecast *forecast) {
  if (!forecast) {
    return;
  }
  kernel_free(forecast->location_name);
  kernel_free(forecast->current_weather_phrase);
  kernel_free(forecast);
}
