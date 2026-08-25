/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

// Firmware-update receive path for the BLE bring-up app. Wire-compatible with
// the real PebbleOS put_bytes.c / firmware_update service.c + system_message.c,
// folded down to a single-transfer receiver that streams the firmware image
// into an OTA slot. See putbytes_min.c.

#pragma once

#include <stdbool.h>
#include <stdint.h>

// Dispatched from ppog_min.c for the two Pebble Protocol endpoints CoreApp
// drives a firmware update over. `p`/`plen` are the PP payload (past the 4-byte
// [length][endpoint] header).
void fw_ota_handle_system_msg(uint16_t conn, const uint8_t *p, uint16_t plen);
void fw_ota_handle_putbytes(uint16_t conn, const uint8_t *p, uint16_t plen);

// Provided by ppog_min.c: send a Pebble Protocol message as a PPoGATT Data
// packet on `endpoint`, advancing the shared TX sequence number.
void ppog_min_send_pp(uint16_t conn, uint16_t endpoint, const uint8_t *payload,
                      uint16_t payload_len);

// --- OTA slot write API ---------------------------------------------------
// The sibling OTA agent's fw_ota_boot.c provides the strong symbols (open a
// slot region, stream bytes, CRC-validate + commit). Until then the weak
// defaults in putbytes_min.c just log, so the receive path builds and runs
// stand-alone. `obj_type` is a PutBytesObjectType (ObjectFirmware/Recovery/
// SysResources).
void ota_slot_begin(uint8_t obj_type, uint32_t total_size, uint32_t append_offset);
int ota_slot_write(uint32_t offset, const uint8_t *buf, uint16_t len);
void ota_slot_finalize(bool success);
