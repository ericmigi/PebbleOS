/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>

#include <host/ble_store.h>
#include <pbl/services/shared_prf_storage/v3_sprf/shared_prf_storage_private.h>

typedef struct {
  struct ble_store_value_sec our_sec;
  struct ble_store_value_sec peer_sec;
  bool our_sec_present;
  bool peer_sec_present;
} SprfBondStoreValues;

void sprf_bond_map_pairing(const SprfBlePairingData *pairing,
                           SprfBondStoreValues *values);

bool sprf_bond_load(SprfBondStoreValues *values);
