/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <host/ble_store.h>
#include <bluetooth/sm_types.h>

typedef struct {
  struct ble_store_value_sec our_sec;
  struct ble_store_value_sec peer_sec;
  bool our_sec_present;
  bool peer_sec_present;
} SprfBondStoreValues;

// Shared-PRF pairing record (what PRF / the FreeRTOS stack persist) <-> the
// NimBLE store values the host consumes.
void sprf_bond_from_info(const SMPairingInfo *info, uint8_t flags,
                         SprfBondStoreValues *values);
void sprf_bond_to_info(const struct ble_store_value_sec *our,
                       const struct ble_store_value_sec *peer,
                       SMPairingInfo *info, uint8_t *flags);

bool sprf_bond_load(SprfBondStoreValues *values);
void sprf_bond_save(const struct ble_store_value_sec *our,
                    const struct ble_store_value_sec *peer);
void sprf_bond_erase(void);
