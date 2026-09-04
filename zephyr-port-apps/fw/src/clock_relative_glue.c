/* SPDX-License-Identifier: Apache-2.0 */

// Relative-time formatting for the timeline layouts (notification "Now" /
// "5 minutes ago" timestamp, reminder "In 10 minutes", etc). Lifted verbatim
// from src/fw/services/clock/service.c so the rendered text is byte-identical to
// the FreeRTOS reference; the full clock/service.c can't be compiled into the
// port because it re-defines a dozen clock_* symbols the port already provides
// (port.c, app_service_stubs.c, apps_port_glue.c, shell_glue.c) and pulls the
// timezone database + prefs. The port renders UTC (no timezone), so gmtime_r
// stands in for the shipping localtime_r.

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "pbl/services/clock.h"
#include "pbl/services/i18n/i18n.h"
#include "util/time/time.h"
#include "util/string.h"

extern time_t rtc_get_time(void);

typedef enum {
  RoundTypeHalfUp,
  RoundTypeHalfDown,
  RoundTypeAlwaysUp,
  RoundTypeAlwaysDown,
} RoundType;

typedef enum {
  FullStyleLower12h,
  FullStyleCapital12h,
  FullStyleLower24h,
  FullStyleCapital24h,
} FullStyle;

static time_t prv_round(time_t round_me, time_t multiple, int round_type) {
  switch (round_type) {
    case RoundTypeHalfDown:
      return ((round_me + multiple / 2 - 1) / multiple) * multiple;
    case RoundTypeAlwaysUp:
      return ((round_me + multiple - 1) / multiple) * multiple;
    case RoundTypeAlwaysDown:
      return (round_me / multiple) * multiple;
    case RoundTypeHalfUp:
    default:
      return ((round_me + multiple / 2) / multiple) * multiple;
  }
}

static size_t prv_format_time_tm(char *buffer, int buf_size, const char *format,
                                 const struct tm *time_tm) {
  const size_t ret_val = strftime(buffer, buf_size, i18n_get(format, buffer), time_tm);
  i18n_free(format, buffer);
  return ret_val;
}

static size_t prv_format_time(char *buffer, int buf_size, const char *format, time_t timestamp) {
  struct tm time_tm;
  gmtime_r(&timestamp, &time_tm);
  return prv_format_time_tm(buffer, buf_size, format, &time_tm);
}

static void prv_clock_get_full_relative_time(char *buffer, int buf_size, time_t timestamp,
                                             bool capitalized, bool with_fulltime) {
  time_t today_midnight = time_util_get_midnight_of(rtc_get_time());
  time_t timestamp_midnight = time_util_get_midnight_of(timestamp);
  time_t yesterday_midnight = time_util_get_midnight_of(rtc_get_time() - SECONDS_PER_DAY);
  time_t last_week_midnight = time_util_get_midnight_of(rtc_get_time() - SECONDS_PER_WEEK);
  time_t next_week_midnight = time_util_get_midnight_of(rtc_get_time() + SECONDS_PER_WEEK);

  const char *time_fmt = NULL;
  int style;
  if (clock_is_24h_style()) {
    style = capitalized ? FullStyleCapital24h : FullStyleLower24h;
  } else {
    style = capitalized ? FullStyleCapital12h : FullStyleLower12h;
  }

  if (timestamp_midnight == today_midnight) {
    switch (style) {
      case FullStyleLower12h:
      case FullStyleCapital12h:
        time_fmt = i18n_noop("%l:%M %p");
        break;
      case FullStyleLower24h:
      case FullStyleCapital24h:
        time_fmt = i18n_noop("%R");
        break;
    }
  } else if (timestamp_midnight == yesterday_midnight) {
    switch (style) {
      case FullStyleLower12h:
      case FullStyleCapital12h:
        time_fmt = with_fulltime ? i18n_noop("Yesterday, %l:%M %p") : i18n_noop("Yesterday");
        break;
      case FullStyleLower24h:
      case FullStyleCapital24h:
        time_fmt = with_fulltime ? i18n_noop("Yesterday, %R") : i18n_noop("Yesterday");
        break;
    }
  } else if (timestamp_midnight <= last_week_midnight || timestamp_midnight >= next_week_midnight) {
    switch (style) {
      case FullStyleLower12h:
      case FullStyleCapital12h:
        time_fmt = with_fulltime ? i18n_noop("%b %e, %l:%M %p") : i18n_noop("%B %e");
        break;
      case FullStyleLower24h:
      case FullStyleCapital24h:
        time_fmt = with_fulltime ? i18n_noop("%b %e, %R") : i18n_noop("%B %e");
        break;
    }
  } else {
    switch (style) {
      case FullStyleLower12h:
      case FullStyleCapital12h:
        time_fmt = with_fulltime ? i18n_noop("%a, %l:%M %p") : i18n_noop("%A");
        break;
      case FullStyleLower24h:
      case FullStyleCapital24h:
        time_fmt = with_fulltime ? i18n_noop("%a, %R") : i18n_noop("%A");
        break;
    }
  }
  prv_format_time(buffer, buf_size, time_fmt, timestamp);
}

static void prv_clock_get_relative_time_string(char *buffer, int buf_size, time_t timestamp,
                                               bool capitalized, int max_relative_hrs,
                                               bool with_fulltime) {
  time_t difference = rtc_get_time() - timestamp;
  time_t today_midnight = time_util_get_midnight_of(rtc_get_time());
  time_t timestamp_midnight = time_util_get_midnight_of(timestamp);

  if (today_midnight != timestamp_midnight) {
    prv_clock_get_full_relative_time(buffer, buf_size, timestamp, capitalized, with_fulltime);
  } else if (difference >= (SECONDS_PER_HOUR * max_relative_hrs)) {
    prv_clock_get_full_relative_time(buffer, buf_size, timestamp, capitalized, with_fulltime);
  } else if (difference >= SECONDS_PER_HOUR) {
    const int num_hrs = prv_round(difference, SECONDS_PER_HOUR, RoundTypeHalfUp) / SECONDS_PER_HOUR;
    const char *str_fmt;
    if (capitalized) {
      str_fmt = i18n_noop("%lu H AGO");
    } else if (num_hrs == 1) {
      str_fmt = i18n_noop("An hour ago");
    } else {
      str_fmt = i18n_noop("%lu hours ago");
    }
    snprintf(buffer, buf_size, i18n_get(str_fmt, buffer), num_hrs);
  } else if (difference >= SECONDS_PER_MINUTE) {
    const int num_minutes =
        prv_round(difference, SECONDS_PER_MINUTE, RoundTypeAlwaysDown) / SECONDS_PER_MINUTE;
    const char *str_fmt;
    if (capitalized) {
      str_fmt = i18n_noop("%lu MIN AGO");
    } else if (num_minutes == 1) {
      str_fmt = i18n_noop("%lu minute ago");
    } else {
      str_fmt = i18n_noop("%lu minutes ago");
    }
    snprintf(buffer, buf_size, i18n_get(str_fmt, buffer), num_minutes);
  } else if (difference >= 0) {
    strncpy(buffer, capitalized ? i18n_get("NOW", buffer) : i18n_get("Now", buffer), buf_size);
  } else if (difference >= -(SECONDS_PER_HOUR - SECONDS_PER_MINUTE)) {
    const int num_minutes =
        prv_round(-difference, SECONDS_PER_MINUTE, RoundTypeAlwaysUp) / SECONDS_PER_MINUTE;
    const char *str_fmt;
    if (capitalized) {
      str_fmt = i18n_noop("IN %lu MIN");
    } else if (num_minutes == 1) {
      str_fmt = i18n_noop("In %lu minute");
    } else {
      str_fmt = i18n_noop("In %lu minutes");
    }
    snprintf(buffer, buf_size, i18n_get(str_fmt, buffer), num_minutes);
  } else if (difference >= -(SECONDS_PER_HOUR * max_relative_hrs)) {
    const int num_hrs = prv_round(-difference, SECONDS_PER_HOUR, RoundTypeHalfDown) / SECONDS_PER_HOUR;
    const char *str_fmt;
    if (capitalized) {
      str_fmt = i18n_noop("IN %lu H");
    } else if (num_hrs == 1) {
      str_fmt = i18n_noop("In %lu hour");
    } else {
      str_fmt = i18n_noop("In %lu hours");
    }
    snprintf(buffer, buf_size, i18n_get(str_fmt, buffer), num_hrs);
  } else {
    prv_clock_get_full_relative_time(buffer, buf_size, timestamp, capitalized, with_fulltime);
  }
  i18n_free_all(buffer);
}

void clock_get_since_time(char *buffer, int buf_size, time_t timestamp) {
  const time_t now = rtc_get_time();
  const time_t clamped_timestamp = (now < timestamp) ? now : timestamp;
  prv_clock_get_relative_time_string(buffer, buf_size, clamped_timestamp, false, HOURS_PER_DAY,
                                     true);
}

void clock_get_until_time(char *buffer, int buf_size, time_t timestamp, int max_relative_hrs) {
  prv_clock_get_relative_time_string(buffer, buf_size, timestamp, false, max_relative_hrs, true);
}

size_t clock_get_time_number(char *number_buffer, size_t number_buffer_size, time_t timestamp) {
  const size_t written =
      prv_format_time(number_buffer, number_buffer_size,
                      (clock_is_24h_style() ? i18n_noop("%R") : i18n_noop("%l:%M")), timestamp);
  const char *number_buffer_ptr = string_strip_leading_whitespace(number_buffer);
  memmove(number_buffer, number_buffer_ptr,
          number_buffer_size - (number_buffer_ptr - number_buffer));
  return written - (number_buffer_ptr - number_buffer);
}

size_t clock_get_time_word(char *buffer, size_t buffer_size, time_t timestamp) {
  if (clock_is_24h_style()) {
    buffer[0] = '\0';
    return 0;
  }
  return prv_format_time(buffer, buffer_size, i18n_noop("%p"), timestamp);
}

bool time_util_range_spans_day(time_t start, time_t end, time_t start_of_day) {
  return (start <= start_of_day && end >= (start_of_day + SECONDS_PER_DAY));
}
