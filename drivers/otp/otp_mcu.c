/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdio.h>

#include <pbl/drivers/mcu.h>
#include <pbl/drivers/otp.h>

// 12 hex digits: the last 6 bytes of the MCU's unique ID
#define SERIAL_BYTES 6U

static char s_serial[2U * SERIAL_BYTES + 1U];
static char s_empty[1];

char *otp_get_slot(const uint8_t index) {
#if defined(CONFIG_BOARD_XTEINK_X4_PRO) || defined(CONFIG_BOARD_T_DECK_PRO)
#ifdef CONFIG_BOARD_T_DECK_PRO
  static char board_revision[] = "tdeck";
#else
  static char board_revision[] = "x4pro";
#endif
  if (index == OTP_HWVER) {
    return board_revision;
  }
#endif
  if (index != OTP_SERIAL) {
    return s_empty;
  }

  if (s_serial[0] == '\0') {
    uint8_t id[16];
    size_t len = sizeof(id);

    if (mcu_get_serial(id, &len) == 0 && len >= SERIAL_BYTES) {
      for (size_t i = 0U; i < SERIAL_BYTES; i++) {
        snprintf(&s_serial[2U * i], 3U, "%02X", id[len - SERIAL_BYTES + i]);
      }
    }
  }

  return s_serial;
}

bool otp_is_locked(const uint8_t index) {
  return true;
}

int otp_write_slot(const uint8_t index, const char *value) {
  return -EEXIST;
}
