/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

// Minimal, wire-compatible PebbleProtocol firmware-update RECEIVE path for the
// BLE bring-up app. It answers the system-endpoint (0x12) FirmwareStart/Status/
// Complete/Fail negotiation and runs the PutBytes object transfer (endpoint
// 0xBEEF) the way the real firmware does, then streams the received firmware
// image into an OTA slot via the ota_slot_* API.
//
// The struct layouts, command/response codes, token/ACK/NACK handshake and the
// legacy commit checksum are lifted verbatim from the real PebbleOS sources so
// the on-wire behaviour matches what CoreApp expects:
//   - src/fw/services/put_bytes/put_bytes.c        (PutBytes state machine)
//   - src/fw/kernel/system_message.c + .h          (system-message flow)
//   - src/fw/services/firmware_update/service.c     (start negotiation)
//   - src/fw/util/legacy_checksum.c                 (commit CRC, compiled in)
//
// ponytail: single in-flight transfer, synchronous ACKs, no pre-ack windowing,
// no timers, no resume-from-status. This is the folded analogue of ppog_min.c
// (which is the folded analogue of ppogatt.c). Grow into the real put_bytes.c +
// comm_session/system_task infra only when this app gains that plumbing.
//
// OUT OF SCOPE for this pass (ponytail-marked TODOs, see report):
//   - signature / key verification of the received image
//   - resource pbpack (SysResources) banks are received + CRC'd but the slot
//     layout for them is the sibling agent's concern
//   - reboot-into-new-firmware handoff (we never reset)

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/sys/printk.h>

#include "fw_ota.h"
#include "util/legacy_checksum.h"

// PutBytesObjectType, mirrored from include/pbl/services/put_bytes/put_bytes.h.
#define OBJ_UNKNOWN 0x00
#define OBJ_FIRMWARE 0x01
#define OBJ_RECOVERY 0x02
#define OBJ_SYS_RESOURCES 0x03

// PutBytesCommand, from put_bytes.c.
#define PB_CMD_INIT 0x01
#define PB_CMD_PUT 0x02
#define PB_CMD_COMMIT 0x03
#define PB_CMD_ABORT 0x04
#define PB_CMD_INSTALL 0x05

// ResponseCode, from put_bytes.c.
#define PB_RESP_ACK 0x01
#define PB_RESP_NACK 0x02

// SystemMessageType, from system_message.h.
#define SYSMSG_FW_START 0x01
#define SYSMSG_FW_COMPLETE 0x02
#define SYSMSG_FW_FAIL 0x03
#define SYSMSG_FW_START_RESPONSE 0x0a
#define SYSMSG_FW_STATUS 0x0b
#define SYSMSG_FW_STATUS_RESPONSE 0x0c

// FirmwareUpdateStatus, from firmware_update.h.
#define FW_UPDATE_STOPPED 0
#define FW_UPDATE_RUNNING 1

#define PP_ENDPOINT_SYSTEM 0x0012
#define PP_ENDPOINT_PUT_BYTES 0xBEEF

// InitRequest append-offset magic, from put_bytes.c prv_do_init().
#define PB_APPEND_OFFSET_MAGIC 0xBE4354EFUL

// Single-transfer receive state.
static struct {
  bool active;            // a FirmwareStart has been seen
  bool have_transfer;     // an Init has set up an in-flight object
  uint32_t token;
  uint8_t obj_type;
  uint32_t total_size;
  uint32_t append_offset;
  uint32_t written;       // bytes streamed into the slot for the current object
  LegacyChecksum crc;     // running legacy checksum of the received data
  // Sticky "both received" tracking so Install can report completion, mirroring
  // s_ready_to_install[] in put_bytes.c (fw + sys resources -> done).
  bool got_firmware;
  bool got_resources;
} s;

static uint32_t prv_be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
         (uint32_t)p[3];
}

static void prv_wr_be32(uint8_t *p, uint32_t v) {
  p[0] = v >> 24;
  p[1] = v >> 16;
  p[2] = v >> 8;
  p[3] = v;
}

static void prv_wr_le32(uint8_t *p, uint32_t v) {
  p[0] = v;
  p[1] = v >> 8;
  p[2] = v >> 16;
  p[3] = v >> 24;
}

// PutBytes response: [response_code:1][token:be32], from prv_send_response().
static void prv_pb_response(uint16_t conn, uint8_t code, uint32_t token) {
  uint8_t msg[5];
  msg[0] = code;
  prv_wr_be32(&msg[1], token);
  ppog_min_send_pp(conn, PP_ENDPOINT_PUT_BYTES, msg, sizeof(msg));
}

// -------------------------------------------------------------------------
// Weak OTA slot hooks. The sibling agent's fw_ota_boot.c overrides these.
// -------------------------------------------------------------------------
__attribute__((weak)) void ota_slot_begin(uint8_t obj_type, uint32_t total_size,
                                          uint32_t append_offset) {
  printk("FW_OTA_SLOT_BEGIN_STUB type=%u size=%u append=%u (no slot backend)\n",
         obj_type, total_size, append_offset);
}

__attribute__((weak)) int ota_slot_write(uint32_t offset, const uint8_t *buf,
                                         uint16_t len) {
  (void)buf;
  // No backend yet: accept the bytes so the receive path exercises end to end.
  (void)offset;
  (void)len;
  return 0;
}

__attribute__((weak)) void ota_slot_finalize(bool success) {
  printk("FW_OTA_SLOT_FINALIZE_STUB success=%d (no slot backend)\n", success);
}

// -------------------------------------------------------------------------
// PutBytes command handlers (endpoint 0xBEEF).
// -------------------------------------------------------------------------

static bool prv_obj_is_fw_update(uint8_t type) {
  return (type == OBJ_FIRMWARE || type == OBJ_RECOVERY || type == OBJ_SYS_RESOURCES);
}

static void prv_do_init(uint16_t conn, const uint8_t *p, uint16_t plen) {
  // InitRequest: [cmd:1][total_size:be32][type:7|has_cookie:1][index:1 | cookie:be32]
  // then an optional trailing InitRequestExtraInfo {magic:be32, append_offset:be32}.
  if (plen < 7) {
    prv_pb_response(conn, PB_RESP_NACK, 0);
    return;
  }
  uint32_t total_size = prv_be32(&p[1]);
  uint8_t type_byte = p[5];
  uint8_t type = type_byte & 0x7F;
  bool has_cookie = (type_byte >> 7) & 0x1;

  if (!prv_obj_is_fw_update(type)) {
    // App/file/worker/app-resources installs need the filesystem + app storage
    // this bring-up app doesn't carry. Only OTA firmware objects are handled.
    printk("FW_OTA_PUTBYTES_INIT_REJECT type=%u (not an OTA object)\n", type);
    prv_pb_response(conn, PB_RESP_NACK, 0);
    return;
  }

  uint32_t append_offset = 0;
  if (plen >= 8 + 6) {  // has room for a trailing 8-byte extra_info blob
    const uint8_t *info = &p[plen - 8];
    if (prv_be32(info) == PB_APPEND_OFFSET_MAGIC) {
      append_offset = prv_be32(info + 4);
      printk("FW_OTA_PUTBYTES_RESUME append=%u\n", append_offset);
    }
  }

  // Generate a non-zero token (rand() is unavailable/cheap here; derive one).
  static uint32_t s_token_seed;
  s_token_seed += 0x9E3779B9u;  // golden-ratio step, avoids repeats
  uint32_t token = s_token_seed ? s_token_seed : 1;

  s.have_transfer = true;
  s.token = token;
  s.obj_type = type;
  s.total_size = total_size;
  s.append_offset = append_offset;
  s.written = append_offset;
  legacy_defective_checksum_init(&s.crc);

  printk("FW_OTA_PUTBYTES_BEGIN type=%u size=%u append=%u tok=%u\n", type,
         total_size, append_offset, token);
  ota_slot_begin(type, total_size, append_offset);

  prv_pb_response(conn, PB_RESP_ACK, token);
}

static void prv_do_put(uint16_t conn, const uint8_t *p, uint16_t plen) {
  // PutRequest: [cmd:1][token:be32][length:be32][data...]
  if (plen < 9) {
    prv_pb_response(conn, PB_RESP_NACK, 0);
    return;
  }
  uint32_t token = prv_be32(&p[1]);
  uint32_t data_len = prv_be32(&p[5]);

  if (!s.have_transfer || token != s.token) {
    printk("FW_OTA_PUTBYTES_PUT_BADTOK got=%u want=%u\n", token, s.token);
    prv_pb_response(conn, PB_RESP_NACK, token);
    return;
  }
  if (data_len > (uint32_t)(plen - 9)) {
    printk("FW_OTA_PUTBYTES_PUT_TRUNC len=%u avail=%u\n", data_len, plen - 9);
    prv_pb_response(conn, PB_RESP_NACK, token);
    return;
  }

  const uint8_t *data = &p[9];
  int rc = ota_slot_write(s.written, data, (uint16_t)data_len);
  if (rc != 0) {
    printk("FW_OTA_PUTBYTES_WRITE_ERR rc=%d off=%u\n", rc, s.written);
    ota_slot_finalize(false);
    s.have_transfer = false;
    prv_pb_response(conn, PB_RESP_NACK, token);
    return;
  }
  legacy_defective_checksum_update(&s.crc, data, data_len);
  s.written += data_len;

  printk("FW_OTA_PUTBYTES %u/%u\n", s.written, s.total_size);
  prv_pb_response(conn, PB_RESP_ACK, token);
}

static void prv_do_commit(uint16_t conn, const uint8_t *p, uint16_t plen) {
  // CommitRequest: [cmd:1][token:be32][crc:be32]
  if (plen < 9) {
    prv_pb_response(conn, PB_RESP_NACK, 0);
    return;
  }
  uint32_t token = prv_be32(&p[1]);
  uint32_t expected_crc = prv_be32(&p[5]);
  if (!s.have_transfer || token != s.token) {
    prv_pb_response(conn, PB_RESP_NACK, token);
    return;
  }

  uint32_t calc = legacy_defective_checksum_finish(&s.crc);
  bool ok = (calc == expected_crc);
  printk("FW_OTA_PUTBYTES_COMMIT calc=0x%08x expected=0x%08x %s\n", calc,
         expected_crc, ok ? "MATCH" : "MISMATCH");

  if (ok) {
    if (s.obj_type == OBJ_FIRMWARE) {
      s.got_firmware = true;
    } else if (s.obj_type == OBJ_SYS_RESOURCES) {
      s.got_resources = true;
    }
  }
  ota_slot_finalize(ok);
  prv_pb_response(conn, ok ? PB_RESP_ACK : PB_RESP_NACK, token);
}

static void prv_do_install(uint16_t conn, const uint8_t *p, uint16_t plen) {
  // InstallRequest: [cmd:1][token:be32]. Mirrors prv_do_install(): a firmware +
  // sys-resources pair marks the update ready. We stop short of setting boot
  // bits / rebooting (out of scope).
  uint32_t token = (plen >= 5) ? prv_be32(&p[1]) : 0;
  if (s.got_firmware && s.got_resources) {
    printk("FW_OTA_PUTBYTES_DONE fw+resources received (reboot handoff TODO)\n");
    s.got_firmware = false;
    s.got_resources = false;
  } else {
    printk("FW_OTA_PUTBYTES_INSTALL partial fw=%d res=%d\n", s.got_firmware,
           s.got_resources);
  }
  s.have_transfer = false;
  prv_pb_response(conn, PB_RESP_ACK, token);
}

void fw_ota_handle_putbytes(uint16_t conn, const uint8_t *p, uint16_t plen) {
  if (plen < 1) {
    return;
  }
  switch (p[0]) {
    case PB_CMD_INIT:
      prv_do_init(conn, p, plen);
      break;
    case PB_CMD_PUT:
      prv_do_put(conn, p, plen);
      break;
    case PB_CMD_COMMIT:
      prv_do_commit(conn, p, plen);
      break;
    case PB_CMD_ABORT:
      printk("FW_OTA_PUTBYTES_ABORT\n");
      ota_slot_finalize(false);
      s.have_transfer = false;
      prv_pb_response(conn, PB_RESP_ACK,
                      (plen >= 5) ? prv_be32(&p[1]) : 0);
      break;
    case PB_CMD_INSTALL:
      prv_do_install(conn, p, plen);
      break;
    default:
      printk("FW_OTA_PUTBYTES_BADCMD 0x%02x\n", p[0]);
      break;
  }
}

// -------------------------------------------------------------------------
// System-message handlers (endpoint 0x12): the FW update negotiation.
// -------------------------------------------------------------------------

static void prv_send_fw_start_response(uint16_t conn, uint8_t status) {
  // system_message_send_firmware_start_response(): [zero:1][type:1][status:1].
  uint8_t msg[3] = {0x00, SYSMSG_FW_START_RESPONSE, status};
  ppog_min_send_pp(conn, PP_ENDPOINT_SYSTEM, msg, sizeof(msg));
}

static void prv_send_fw_status_response(uint16_t conn) {
  // prv_handle_firmware_status_request(): native-endian (LE) packed struct.
  // [deprecated:1][type:1][rsvd:2][res_bytes:4][res_crc:4][fw_bytes:4][fw_crc:4]
  uint8_t msg[20];
  memset(msg, 0, sizeof(msg));
  msg[1] = SYSMSG_FW_STATUS_RESPONSE;
  uint32_t fw_bytes = (s.have_transfer && s.obj_type == OBJ_FIRMWARE) ? s.written : 0;
  prv_wr_le32(&msg[12], fw_bytes);  // firmware_bytes_written
  ppog_min_send_pp(conn, PP_ENDPOINT_SYSTEM, msg, sizeof(msg));
}

void fw_ota_handle_system_msg(uint16_t conn, const uint8_t *p, uint16_t plen) {
  // System message: [deprecated/zero:1][type:1][payload...].
  if (plen < 2) {
    return;
  }
  uint8_t type = p[1];
  switch (type) {
    case SYSMSG_FW_START: {
      // Optional SysMsgSmoothFirmwareStartPayload: [.. type][already:be32?][to:be32?].
      // The real fw reads these little-endian off the packed struct; the mobile
      // side sends them big-endian on the wire only for PP framing fields, but
      // this payload is memcpy'd raw, so treat it native (LE). We only log it.
      s.active = true;
      s.got_firmware = false;
      s.got_resources = false;
      printk("FW_OTA_SYSMSG_FW_START -> RUNNING\n");
      prv_send_fw_start_response(conn, FW_UPDATE_RUNNING);
      break;
    }
    case SYSMSG_FW_STATUS:
      prv_send_fw_status_response(conn);
      break;
    case SYSMSG_FW_COMPLETE:
      printk("FW_OTA_SYSMSG_FW_COMPLETE (no reboot)\n");
      s.active = false;
      break;
    case SYSMSG_FW_FAIL:
      printk("FW_OTA_SYSMSG_FW_FAIL\n");
      ota_slot_finalize(false);
      s.active = false;
      s.have_transfer = false;
      break;
    default:
      printk("FW_OTA_SYSMSG type=%u (ignored)\n", type);
      break;
  }
}
