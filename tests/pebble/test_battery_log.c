/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "services/battery/battery_log.h"

#include <stdint.h>
#include <string.h>
#include <time.h>

#include <pbl/fs/fs.h>

#include <clar.h>
#include <fake_pfs.h>
#include <fake_spi_flash.h>

static time_t s_now;

time_t sys_get_time(void) {
  return s_now;
}

typedef struct __attribute__((packed)) {
  uint32_t utc;
  uint16_t millivolts;
  uint8_t percent;
  uint8_t usb;
} Record;

// The records of a log file, until the first empty one
static int prv_read(const char *path, Record *records, int max) {
  struct pbl_fs_file file;
  if (pbl_fs_open(&file, path, PBL_FS_O_READ) < 0) {
    return -1;
  }
  int n = 0;
  while ((n < max) && (pbl_fs_read(&file, &records[n], sizeof(Record)) == sizeof(Record)) &&
         (records[n].utc != UINT32_MAX) && (records[n].utc != 0)) {
    n++;
  }
  pbl_fs_close(&file);
  return n;
}

void test_battery_log__initialize(void) {
  fake_spi_flash_init(0, 0x1000000);
  fake_pfs_mount();
  pbl_fs_format("/");
  s_now = 1795611600;
}

void test_battery_log__records_once_a_minute(void) {
  battery_log_sample(4100, 90, false);
  s_now += 30;
  battery_log_sample(4090, 90, false); // too soon
  s_now += 30;
  battery_log_sample(4080, 89, true);

  Record r[4];
  cl_assert_equal_i(prv_read("/batlog0", r, 4), 2);
  cl_assert_equal_i(r[0].utc, 1795611600);
  cl_assert_equal_i(r[0].millivolts, 4100);
  cl_assert_equal_i(r[0].percent, 90);
  cl_assert_equal_i(r[0].usb, 0);
  cl_assert_equal_i(r[1].utc, 1795611660);
  cl_assert_equal_i(r[1].millivolts, 4080);
  cl_assert_equal_i(r[1].usb, 1);
}

void test_battery_log__rotates_when_full(void) {
  // Two files' worth and two more: the first file's records are gone
  const int n = CONFIG_SERVICE_BATTERY_LOG_RECORDS;
  for (int i = 0; i < 2 * n + 2; i++) {
    battery_log_sample(4000 - i, 80, false);
    s_now += 60;
  }

  Record r[CONFIG_SERVICE_BATTERY_LOG_RECORDS];
  cl_assert_equal_i(prv_read("/batlog1", r, n), n);
  cl_assert_equal_i(r[0].millivolts, 4000 - n);
  cl_assert_equal_i(prv_read("/batlog0", r, n), 2);
  cl_assert_equal_i(r[0].millivolts, 4000 - 2 * n);
  cl_assert_equal_i(r[1].millivolts, 4000 - 2 * n - 1);
}
