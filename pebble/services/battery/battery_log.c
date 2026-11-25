/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

// Discharge log: the battery voltage, percent and USB presence once a minute, in the filesystem
// so it survives reboots and a cell browning out. Two files take turns: one is filled in order,
// and when it is full the other is emptied and filled next, so the oldest records go. Each record
// goes to flash as it is taken, to a place of its own that was erased when the file was made: a
// write cut short spoils that record alone, and a reboot carries on after the last one.
// `battery log` prints them all.

#include "battery_log.h"

#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include <pbl/fs/fs.h>
#include <pbl/kernel/compiler.h>
#include <pbl/logging/logging.h>

#include <syscall/syscall.h>

PBL_LOG_MODULE_DECLARE(service_battery, CONFIG_SERVICE_BATTERY_LOG_LEVEL);

#define RECORDS        CONFIG_SERVICE_BATTERY_LOG_RECORDS
#define MIN_INTERVAL_S 60

typedef struct PBL_PACKED {
  uint32_t utc;
  uint16_t millivolts;
  uint8_t percent;
  uint8_t flags; // BATTERY_LOG_FLAG_*
} BatteryLogRecord;

#define BATTERY_LOG_FLAG_USB  0x01
#define BATTERY_LOG_FLAG_BOOT 0x02 // The first record since the firmware started

static const char *const s_paths[2] = {"/batlog0", "/batlog1"};

static int s_current = -1; // The file being filled; -1 until found after boot
static int s_next;         // Its next record
static uint32_t s_last_utc;
static bool s_logged; // A record was taken since boot

// A record never written reads as erased flash. The clock may read zero after a brown-out.
static bool prv_is_empty(const BatteryLogRecord *record) {
  return (record->utc == UINT32_MAX) && (record->millivolts == UINT16_MAX);
}

// The records of a file, and when its first was taken (0 if none).
static int prv_count(int i, uint32_t *first_utc) {
  struct pbl_fs_file file;
  *first_utc = 0;
  if (pbl_fs_open(&file, s_paths[i], PBL_FS_O_READ) < 0) {
    return 0;
  }
  BatteryLogRecord record;
  int n = 0;
  while ((n < RECORDS) && (pbl_fs_read(&file, &record, sizeof(record)) == sizeof(record)) &&
         !prv_is_empty(&record)) {
    if (n == 0) {
      *first_utc = record.utc;
    }
    n++;
  }
  pbl_fs_close(&file);
  return n;
}

// The file being filled: one with room, the fuller if both have; if neither does, the older,
// which is emptied.
static void prv_find_current(void) {
  uint32_t first[2];
  const int count[2] = {prv_count(0, &first[0]), prv_count(1, &first[1])};
  if ((count[0] < RECORDS) && (count[1] < RECORDS)) {
    s_current = (count[1] > count[0]) ? 1 : 0;
  } else if (count[0] < RECORDS) {
    s_current = 0;
  } else if (count[1] < RECORDS) {
    s_current = 1;
  } else {
    s_current = (first[1] < first[0]) ? 1 : 0;
    pbl_fs_unlink(s_paths[s_current]);
  }
  s_next = (count[s_current] < RECORDS) ? count[s_current] : 0;
}

static int prv_open_current(struct pbl_fs_file *file) {
  int ret = pbl_fs_open(file, s_paths[s_current], PBL_FS_O_RDWR | PBL_FS_O_CREATE);
  if ((ret == 0) && (pbl_fs_size(file) == 0)) {
    // A new file, as after `battery log clear`: it starts empty
    s_next = 0;
    ret = pbl_fs_truncate(file, RECORDS * sizeof(BatteryLogRecord));
    if (ret < 0) {
      pbl_fs_close(file);
    }
  }
  return ret;
}

void battery_log_sample(uint16_t millivolts, uint8_t percent, bool usb) {
  const uint32_t utc = (uint32_t)sys_get_time();
  // A clock set back starts the interval over
  if (s_logged && (utc >= s_last_utc) && (utc - s_last_utc < MIN_INTERVAL_S)) {
    return;
  }
  s_last_utc = utc;

  if (s_current < 0) {
    prv_find_current();
  }
  if (s_next >= RECORDS) {
    // Full: the other file is emptied and filled next
    s_current ^= 1;
    s_next = 0;
    pbl_fs_unlink(s_paths[s_current]);
  }

  struct pbl_fs_file file;
  if (prv_open_current(&file) < 0) {
    PBL_LOG_WRN("Battery log %s could not be opened", s_paths[s_current]);
    return;
  }
  const BatteryLogRecord record = {
    .utc = utc,
    .millivolts = millivolts,
    .percent = percent,
    .flags = (usb ? BATTERY_LOG_FLAG_USB : 0) | (s_logged ? 0 : BATTERY_LOG_FLAG_BOOT),
  };
  if ((pbl_fs_seek(&file, s_next * (off_t)sizeof(record), PBL_FS_SEEK_SET) >= 0) &&
      (pbl_fs_write(&file, &record, sizeof(record)) == sizeof(record))) {
    s_next++;
    s_logged = true;
  }
  pbl_fs_close(&file);
}

#if UNITTEST
// Forgets what was found since boot, as a reboot does
void battery_log_reset_for_tests(void) {
  s_current = -1;
  s_last_utc = 0;
  s_logged = false;
}
#endif

#ifdef CONFIG_SHELL
#include <pbl/shell/shell.h>

// A write cut short leaves fields erased: a record says what it holds only if it is whole
static bool prv_is_whole(const BatteryLogRecord *record) {
  return (record->percent <= 100) &&
         ((record->flags & ~(BATTERY_LOG_FLAG_USB | BATTERY_LOG_FLAG_BOOT)) == 0);
}

static void prv_print_file(const struct pbl_shell *sh, int i) {
  struct pbl_fs_file file;
  if (pbl_fs_open(&file, s_paths[i], PBL_FS_O_READ) < 0) {
    return;
  }
  BatteryLogRecord record;
  while ((pbl_fs_read(&file, &record, sizeof(record)) == sizeof(record)) &&
         !prv_is_empty(&record)) {
    if (prv_is_whole(&record)) {
      pbl_shell_print(sh, "%" PRIu32 ",%u,%u,%u,%u", record.utc, record.millivolts, record.percent,
                      (record.flags & BATTERY_LOG_FLAG_USB) ? 1 : 0,
                      (record.flags & BATTERY_LOG_FLAG_BOOT) ? 1 : 0);
    }
  }
  pbl_fs_close(&file);
}

static int prv_cmd_log(const struct pbl_shell *sh, size_t argc, char **argv) {
  if ((argc > 1) && !strcmp(argv[1], "clear")) {
    pbl_fs_unlink(s_paths[0]);
    pbl_fs_unlink(s_paths[1]);
    s_current = -1;
    return 0;
  }
  if (s_current < 0) {
    prv_find_current();
  }
  pbl_shell_print(sh, "utc,mv,percent,usb,boot");
  prv_print_file(sh, s_current ^ 1);
  prv_print_file(sh, s_current);
  return 0;
}

PBL_SHELL_SUBCMD_ADD(sub_battery, log, nullptr,
                     "Print the discharge log as CSV, oldest first, or [clear] it", prv_cmd_log, 1,
                     1);
#endif
