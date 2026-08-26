/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

//! Initialize the folded firmware-update protocol state and reboot work item.
void putbytes_min_init(void);

//! Handle payloads from the firmware system-message endpoint (0x0012).
void putbytes_min_handle_system_message(const uint8_t *payload,
                                        uint16_t payload_len);

//! Handle payloads from the PutBytes endpoint (0xBEEF).
void putbytes_min_handle_request(const uint8_t *payload,
                                 uint16_t payload_len);
