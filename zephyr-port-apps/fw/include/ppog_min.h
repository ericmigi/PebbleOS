/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Sends one Pebble Protocol message over the active reversed-PPoGATT link.
// Returns false if the link is not open or the message cannot be notified.
bool ppog_min_send_pp(uint16_t endpoint, const uint8_t *payload,
                      uint16_t payload_len);
