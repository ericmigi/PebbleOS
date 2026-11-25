/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <inttypes.h>
#include <stdint.h>

#include <pbl/crc/crc.h>
#include <pbl/logging/logging.h>
#include <pbl/system/rtc_registers.h>

// Start of ESP32-S3 RTC FAST memory (0x600fe000), preserved across software and watchdog resets
#define RETAINED_BASE 0x600fe000U

#define RETAINED_COUNT (NRF_RETAINED_REGISTER_CRC + 1)

static volatile uint32_t *const s_retained = (volatile uint32_t *)RETAINED_BASE;

static uint32_t prv_crc(void) {
  return pbl_crc32(0, (const void *)s_retained, NRF_RETAINED_REGISTER_CRC * 4);
}

void retained_write(uint8_t id, uint32_t value) {
  s_retained[id] = value;
  s_retained[NRF_RETAINED_REGISTER_CRC] = prv_crc();
}

uint32_t retained_read(uint8_t id) {
  return s_retained[id];
}

bool retained_validate(void) {
  uint32_t crc = prv_crc();

  if (crc == s_retained[NRF_RETAINED_REGISTER_CRC]) {
    return true;
  }

  PBL_LOG_WRN("Retained register CRC failed: expected %08" PRIx32 ", got %08" PRIx32 ", clearing",
              crc, s_retained[NRF_RETAINED_REGISTER_CRC]);
  for (unsigned int i = 0; i < RETAINED_COUNT; i++) {
    s_retained[i] = 0;
  }

  return false;
}
