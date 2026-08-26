/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

// Wire-compatible, single-transfer firmware-update receiver folded from
// kernel/system_message.c and services/put_bytes/put_bytes.c. It runs directly
// on the minimal PPoGATT endpoint router and streams firmware to slot1.

#include "putbytes_min.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>

#include "fw_ota_boot.h"
#include "ppog_min.h"
#include "util/legacy_checksum.h"

#define OBJECT_FIRMWARE 0x01
#define OBJECT_SYS_RESOURCES 0x03

#define PUT_BYTES_INIT 0x01
#define PUT_BYTES_PUT 0x02
#define PUT_BYTES_COMMIT 0x03
#define PUT_BYTES_ABORT 0x04
#define PUT_BYTES_INSTALL 0x05

#define PUT_BYTES_ACK 0x01
#define PUT_BYTES_NACK 0x02

#define SYSTEM_MESSAGE_FIRMWARE_START 0x01
#define SYSTEM_MESSAGE_FIRMWARE_COMPLETE 0x02
#define SYSTEM_MESSAGE_FIRMWARE_FAIL 0x03
#define SYSTEM_MESSAGE_FIRMWARE_START_RESPONSE 0x0a
#define SYSTEM_MESSAGE_FIRMWARE_STATUS 0x0b
#define SYSTEM_MESSAGE_FIRMWARE_STATUS_RESPONSE 0x0c

#define FIRMWARE_UPDATE_RUNNING 1

#define PP_ENDPOINT_SYSTEM_MESSAGE 0x0012
#define PP_ENDPOINT_PUT_BYTES 0xbeef
#define PUT_BYTES_APPEND_OFFSET_MAGIC 0xbe4354efUL

typedef struct ReadyObject {
  bool valid;
  uint32_t token;
  uint32_t bytes;
  uint32_t checksum;
} ReadyObject;

static struct {
  bool update_active;
  bool have_transfer;
  bool reboot_scheduled;
  uint8_t object_type;
  uint32_t token;
  uint32_t object_size;
  uint32_t written;
  LegacyChecksum checksum;
  ReadyObject firmware;
  ReadyObject resources;
  struct k_work_delayable reboot_work;
} s_putbytes;

static uint32_t s_token_seed;

static uint32_t prv_read_be32(const uint8_t *data) {
  return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
         ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static void prv_write_be32(uint8_t *data, uint32_t value) {
  data[0] = value >> 24;
  data[1] = value >> 16;
  data[2] = value >> 8;
  data[3] = value;
}

static void prv_write_le32(uint8_t *data, uint32_t value) {
  data[0] = value;
  data[1] = value >> 8;
  data[2] = value >> 16;
  data[3] = value >> 24;
}

static uint32_t prv_finish_checksum(const LegacyChecksum *checksum) {
  LegacyChecksum copy = *checksum;
  return legacy_defective_checksum_finish(&copy);
}

static bool prv_send_putbytes_response(uint8_t code, uint32_t token) {
  uint8_t response[5];
  response[0] = code;
  prv_write_be32(response + 1, token);
  return ppog_min_send_pp(PP_ENDPOINT_PUT_BYTES, response, sizeof(response));
}

static bool prv_send_system_message(uint8_t type) {
  const uint8_t response[2] = {0x00, type};
  return ppog_min_send_pp(PP_ENDPOINT_SYSTEM_MESSAGE, response,
                          sizeof(response));
}

static ReadyObject *prv_ready_object(uint8_t object_type) {
  if (object_type == OBJECT_FIRMWARE) {
    return &s_putbytes.firmware;
  }
  if (object_type == OBJECT_SYS_RESOURCES) {
    return &s_putbytes.resources;
  }
  return NULL;
}

static void prv_reboot_work(struct k_work *work) {
  (void)work;
  printk("FW_OTA_INSTALL_REBOOT\n");
  sys_reboot(SYS_REBOOT_COLD);
}

void putbytes_min_init(void) {
  memset(&s_putbytes, 0, sizeof(s_putbytes));
  k_work_init_delayable(&s_putbytes.reboot_work, prv_reboot_work);
}

static void prv_handle_init(const uint8_t *payload, uint16_t payload_len) {
  // InitRequest: cmd, total_size:be32, type:7|has_cookie:1, then index:1
  // (or cookie:be32), optionally followed by the 8-byte resume extra info.
  if (payload_len < 7U) {
    (void)prv_send_putbytes_response(PUT_BYTES_NACK, 0);
    return;
  }

  const uint32_t transfer_size = prv_read_be32(payload + 1);
  const uint8_t type_byte = payload[5];
  const uint8_t object_type = type_byte & 0x7f;
  const bool has_cookie = (type_byte & 0x80) != 0;
  const uint16_t base_length = has_cookie ? 10U : 7U;
  if ((payload_len < base_length) || (transfer_size == 0U) ||
      !s_putbytes.update_active ||
      ((object_type != OBJECT_FIRMWARE) &&
       (object_type != OBJECT_SYS_RESOURCES))) {
    (void)prv_send_putbytes_response(PUT_BYTES_NACK, 0);
    return;
  }

  uint32_t append_offset = 0;
  if (payload_len >= base_length + 8U) {
    const uint8_t *extra = payload + payload_len - 8U;
    if (prv_read_be32(extra) == PUT_BYTES_APPEND_OFFSET_MAGIC) {
      append_offset = prv_read_be32(extra + 4);
    }
  }

  if (append_offset > (UINT32_MAX - transfer_size)) {
    (void)prv_send_putbytes_response(PUT_BYTES_NACK, 0);
    return;
  }
  // CoreApp sends the number of bytes remaining in this Init, not the whole
  // object size, when resumeOffset is present.
  const uint32_t object_size = append_offset + transfer_size;

  const bool continuing =
      s_putbytes.have_transfer &&
      (s_putbytes.object_type == object_type) &&
      (s_putbytes.object_size == object_size) &&
      (s_putbytes.written == append_offset) && (append_offset != 0U);
  if ((append_offset != 0U) && !continuing) {
    (void)prv_send_putbytes_response(PUT_BYTES_NACK, 0);
    return;
  }

  if (s_putbytes.have_transfer && !continuing) {
    if (s_putbytes.object_type == OBJECT_FIRMWARE) {
      fw_ota_slot_abort();
    }
    s_putbytes.have_transfer = false;
  }

  if (object_type == OBJECT_FIRMWARE) {
    int result = fw_ota_slot_begin(object_size, append_offset);
    if (result != 0) {
      printk("FW_OTA_RECV_BEGIN_FAIL rc=%d size=%u append=%u\n", result,
             object_size, append_offset);
      (void)prv_send_putbytes_response(PUT_BYTES_NACK, 0);
      return;
    }
  } else if (append_offset == 0U) {
    printk("FW_OTA_RESOURCES_DISCARD_BEGIN size=%u\n", object_size);
  }

  if (!continuing) {
    legacy_defective_checksum_init(&s_putbytes.checksum);
  }
  ReadyObject *ready = prv_ready_object(object_type);
  if ((ready != NULL) && !continuing) {
    *ready = (ReadyObject){};
  }

  s_token_seed += 0x9e3779b9U;
  s_putbytes.token = (s_token_seed != 0U) ? s_token_seed : 1U;
  s_putbytes.have_transfer = true;
  s_putbytes.object_type = object_type;
  s_putbytes.object_size = object_size;
  s_putbytes.written = append_offset;
  (void)prv_send_putbytes_response(PUT_BYTES_ACK, s_putbytes.token);
}

static void prv_handle_put(const uint8_t *payload, uint16_t payload_len) {
  // PutRequest: cmd, token:be32, length:be32, data.
  if (payload_len < 9U) {
    (void)prv_send_putbytes_response(PUT_BYTES_NACK, 0);
    return;
  }

  const uint32_t token = prv_read_be32(payload + 1);
  const uint32_t data_len = prv_read_be32(payload + 5);
  if (!s_putbytes.have_transfer || (token != s_putbytes.token) ||
      (data_len > (uint32_t)(payload_len - 9U)) ||
      (data_len > (s_putbytes.object_size - s_putbytes.written))) {
    (void)prv_send_putbytes_response(PUT_BYTES_NACK, token);
    return;
  }

  const uint8_t *data = payload + 9;
  if (s_putbytes.object_type == OBJECT_FIRMWARE) {
    int result =
        fw_ota_slot_write(s_putbytes.written, data, data_len);
    if (result != 0) {
      printk("FW_OTA_WRITE_FAIL rc=%d offset=%u length=%u\n", result,
             s_putbytes.written, data_len);
      fw_ota_slot_abort();
      s_putbytes.have_transfer = false;
      (void)prv_send_putbytes_response(PUT_BYTES_NACK, token);
      return;
    }
  }

  legacy_defective_checksum_update(&s_putbytes.checksum, data, data_len);
  s_putbytes.written += data_len;
  printk("FW_OTA_PUT %u/%u\n", s_putbytes.written,
         s_putbytes.object_size);
  (void)prv_send_putbytes_response(PUT_BYTES_ACK, token);
}

static void prv_handle_commit(const uint8_t *payload, uint16_t payload_len) {
  // CommitRequest: cmd, token:be32, legacy_checksum:be32.
  if (payload_len < 9U) {
    (void)prv_send_putbytes_response(PUT_BYTES_NACK, 0);
    return;
  }

  const uint32_t token = prv_read_be32(payload + 1);
  const uint32_t expected = prv_read_be32(payload + 5);
  if (!s_putbytes.have_transfer || (token != s_putbytes.token)) {
    (void)prv_send_putbytes_response(PUT_BYTES_NACK, token);
    return;
  }

  const uint32_t calculated = prv_finish_checksum(&s_putbytes.checksum);
  const bool checksum_matches = (calculated == expected);
  printk("FW_OTA_COMMIT calc=0x%08x expected=0x%08x %s\n", calculated,
         expected, checksum_matches ? "MATCH" : "MISMATCH");

  bool success = checksum_matches &&
                 (s_putbytes.written == s_putbytes.object_size);
  if (success && (s_putbytes.object_type == OBJECT_FIRMWARE)) {
    int result = fw_ota_slot_finish();
    if (result != 0) {
      printk("FW_OTA_IMAGE_INVALID rc=%d\n", result);
      success = false;
    }
  }

  ReadyObject *ready = prv_ready_object(s_putbytes.object_type);
  if (success && (ready != NULL)) {
    *ready = (ReadyObject){
        .valid = true,
        .token = token,
        .bytes = s_putbytes.written,
        .checksum = calculated,
    };
  } else if (s_putbytes.object_type == OBJECT_FIRMWARE) {
    fw_ota_slot_abort();
  }

  s_putbytes.have_transfer = false;
  (void)prv_send_putbytes_response(success ? PUT_BYTES_ACK : PUT_BYTES_NACK,
                                   token);
}

static void prv_handle_abort(const uint8_t *payload, uint16_t payload_len) {
  const uint32_t token =
      (payload_len >= 5U) ? prv_read_be32(payload + 1) : 0;
  if (!s_putbytes.have_transfer || (token != s_putbytes.token)) {
    (void)prv_send_putbytes_response(PUT_BYTES_NACK, token);
    return;
  }

  if (s_putbytes.object_type == OBJECT_FIRMWARE) {
    fw_ota_slot_abort();
  }
  s_putbytes.have_transfer = false;
  (void)prv_send_putbytes_response(PUT_BYTES_ACK, token);
}

static void prv_handle_install(const uint8_t *payload, uint16_t payload_len) {
  if (payload_len < 5U || s_putbytes.have_transfer) {
    (void)prv_send_putbytes_response(PUT_BYTES_NACK, 0);
    return;
  }

  const uint32_t token = prv_read_be32(payload + 1);
  ReadyObject *ready = NULL;
  uint8_t object_type = 0;
  if (s_putbytes.firmware.valid &&
      (s_putbytes.firmware.token == token)) {
    ready = &s_putbytes.firmware;
    object_type = OBJECT_FIRMWARE;
  } else if (s_putbytes.resources.valid &&
             (s_putbytes.resources.token == token)) {
    ready = &s_putbytes.resources;
    object_type = OBJECT_SYS_RESOURCES;
  }

  if ((token == 0U) || (ready == NULL)) {
    (void)prv_send_putbytes_response(PUT_BYTES_NACK, token);
    return;
  }

  if (object_type == OBJECT_SYS_RESOURCES) {
    *ready = (ReadyObject){};
    printk("FW_OTA_RESOURCES_DISCARDED\n");
    (void)prv_send_putbytes_response(PUT_BYTES_ACK, token);
    return;
  }

  const int result = fw_ota_slot_install();
  if (result != 0) {
    printk("FW_OTA_INSTALL_FAIL rc=%d\n", result);
    fw_ota_slot_abort();
    *ready = (ReadyObject){};
    (void)prv_send_putbytes_response(PUT_BYTES_NACK, token);
    return;
  }

  *ready = (ReadyObject){};
  s_putbytes.update_active = false;
  const bool ack_sent =
      prv_send_putbytes_response(PUT_BYTES_ACK, token);
  const bool complete_sent =
      prv_send_system_message(SYSTEM_MESSAGE_FIRMWARE_COMPLETE);
  if (!ack_sent || !complete_sent) {
    printk("FW_OTA_INSTALL_RESPONSE_FAIL ack=%u complete=%u\n", ack_sent,
           complete_sent);
  }

  // Match the production completion delay: it gives NimBLE time to transmit
  // both responses and lets CoreApp Install an optional resources object.
  s_putbytes.reboot_scheduled = true;
  (void)k_work_schedule(&s_putbytes.reboot_work, K_SECONDS(3));
}

void putbytes_min_handle_request(const uint8_t *payload,
                                 uint16_t payload_len) {
  if ((payload == NULL) || (payload_len == 0U)) {
    return;
  }

  switch (payload[0]) {
    case PUT_BYTES_INIT:
      prv_handle_init(payload, payload_len);
      break;
    case PUT_BYTES_PUT:
      prv_handle_put(payload, payload_len);
      break;
    case PUT_BYTES_COMMIT:
      prv_handle_commit(payload, payload_len);
      break;
    case PUT_BYTES_ABORT:
      prv_handle_abort(payload, payload_len);
      break;
    case PUT_BYTES_INSTALL:
      prv_handle_install(payload, payload_len);
      break;
    default: {
      const uint32_t token =
          (payload_len >= 5U) ? prv_read_be32(payload + 1) : 0;
      (void)prv_send_putbytes_response(PUT_BYTES_NACK, token);
      break;
    }
  }
}

static void prv_send_firmware_status(void) {
  // Packed native-endian response from kernel/system_message.c:
  // deprecated, type, reserved[2], resource bytes/crc, firmware bytes/crc.
  uint8_t response[20] = {0};
  response[1] = SYSTEM_MESSAGE_FIRMWARE_STATUS_RESPONSE;

  if (s_putbytes.have_transfer) {
    const uint32_t checksum = prv_finish_checksum(&s_putbytes.checksum);
    if (s_putbytes.object_type == OBJECT_FIRMWARE) {
      prv_write_le32(response + 12, s_putbytes.written);
      prv_write_le32(response + 16, checksum);
    } else if (s_putbytes.object_type == OBJECT_SYS_RESOURCES) {
      prv_write_le32(response + 4, s_putbytes.written);
      prv_write_le32(response + 8, checksum);
    }
  } else {
    if (s_putbytes.firmware.valid) {
      prv_write_le32(response + 12, s_putbytes.firmware.bytes);
      prv_write_le32(response + 16, s_putbytes.firmware.checksum);
    }
    if (s_putbytes.resources.valid) {
      prv_write_le32(response + 4, s_putbytes.resources.bytes);
      prv_write_le32(response + 8, s_putbytes.resources.checksum);
    }
  }

  (void)ppog_min_send_pp(PP_ENDPOINT_SYSTEM_MESSAGE, response,
                         sizeof(response));
}

void putbytes_min_handle_system_message(const uint8_t *payload,
                                        uint16_t payload_len) {
  if ((payload == NULL) || (payload_len < 2U)) {
    return;
  }

  switch (payload[1]) {
    case SYSTEM_MESSAGE_FIRMWARE_START: {
      s_putbytes.update_active = true;
      const uint8_t response[3] = {
          0x00, SYSTEM_MESSAGE_FIRMWARE_START_RESPONSE,
          FIRMWARE_UPDATE_RUNNING,
      };
      (void)ppog_min_send_pp(PP_ENDPOINT_SYSTEM_MESSAGE, response,
                             sizeof(response));
      break;
    }
    case SYSTEM_MESSAGE_FIRMWARE_STATUS:
      prv_send_firmware_status();
      break;
    case SYSTEM_MESSAGE_FIRMWARE_COMPLETE:
      printk("FW_OTA_SYSTEM_COMPLETE\n");
      break;
    case SYSTEM_MESSAGE_FIRMWARE_FAIL:
      if (s_putbytes.reboot_scheduled) {
        (void)k_work_cancel_delayable(&s_putbytes.reboot_work);
      }
      fw_ota_slot_abort();
      s_putbytes.update_active = false;
      s_putbytes.have_transfer = false;
      s_putbytes.reboot_scheduled = false;
      s_putbytes.firmware = (ReadyObject){};
      s_putbytes.resources = (ReadyObject){};
      printk("FW_OTA_SYSTEM_FAIL\n");
      break;
    default:
      break;
  }
}
