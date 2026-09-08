/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "sprf_bond.h"

#include <string.h>

#ifndef SPRF_BOND_HOST_TEST
#include <pbl/services/shared_prf_storage/shared_prf_storage.h>
#endif

#define SPRF_KEY_SIZE 16u
#define SPRF_FLAG_SECURE_CONNECTIONS 0x01u
#define SPRF_FLAG_AUTHENTICATED 0x02u

static void prv_set_peer(const BTDeviceInternal *identity, uint8_t flags,
                         struct ble_store_value_sec *value) {
  value->peer_addr.type =
      identity->is_random_address ? BLE_ADDR_RANDOM : BLE_ADDR_PUBLIC;
  memcpy(value->peer_addr.val, identity->address.octets,
         sizeof(value->peer_addr.val));
  value->key_size = SPRF_KEY_SIZE;
  value->sc = !!(flags & SPRF_FLAG_SECURE_CONNECTIONS);
  value->authenticated = !!(flags & SPRF_FLAG_AUTHENTICATED);
}

void sprf_bond_from_info(const SMPairingInfo *info, uint8_t flags,
                         SprfBondStoreValues *values) {
  memset(values, 0, sizeof(*values));
  if (!info->is_remote_identity_info_valid) {
    return;
  }

  if (info->is_local_encryption_info_valid) {
    struct ble_store_value_sec *our = &values->our_sec;
    prv_set_peer(&info->identity, flags, our);
    our->ediv = info->local_encryption_info.ediv;
    our->rand_num = info->local_encryption_info.rand;
    memcpy(our->ltk, info->local_encryption_info.ltk.data, sizeof(our->ltk));
    our->ltk_present = 1;
    values->our_sec_present = true;
  }

  struct ble_store_value_sec *peer = &values->peer_sec;
  prv_set_peer(&info->identity, flags, peer);
  if (info->is_remote_encryption_info_valid) {
    peer->ediv = info->remote_encryption_info.ediv;
    peer->rand_num = info->remote_encryption_info.rand;
    memcpy(peer->ltk, info->remote_encryption_info.ltk.data, sizeof(peer->ltk));
    peer->ltk_present = 1;
  }
  memcpy(peer->irk, info->irk.data, sizeof(peer->irk));
  peer->irk_present = 1;
  values->peer_sec_present = true;
}

void sprf_bond_to_info(const struct ble_store_value_sec *our,
                       const struct ble_store_value_sec *peer,
                       SMPairingInfo *info, uint8_t *flags) {
  const struct ble_store_value_sec *identity = peer ? peer : our;

  memset(info, 0, sizeof(*info));
  *flags = 0;
  if (!identity) {
    return;
  }

  if (our && our->ltk_present) {
    info->local_encryption_info.ediv = our->ediv;
    info->local_encryption_info.rand = our->rand_num;
    memcpy(info->local_encryption_info.ltk.data, our->ltk, SPRF_KEY_SIZE);
    info->is_local_encryption_info_valid = true;
  }
  if (peer && peer->ltk_present) {
    info->remote_encryption_info.ediv = peer->ediv;
    info->remote_encryption_info.rand = peer->rand_num;
    memcpy(info->remote_encryption_info.ltk.data, peer->ltk, SPRF_KEY_SIZE);
    info->is_remote_encryption_info_valid = true;
  }
  if (peer && peer->irk_present) {
    memcpy(info->irk.data, peer->irk, SPRF_KEY_SIZE);
  }
  memcpy(info->identity.address.octets, identity->peer_addr.val,
         sizeof(info->identity.address.octets));
  info->identity.is_random_address = identity->peer_addr.type == BLE_ADDR_RANDOM;
  info->is_remote_identity_info_valid = true;

  *flags = (identity->sc ? SPRF_FLAG_SECURE_CONNECTIONS : 0) |
           (identity->authenticated ? SPRF_FLAG_AUTHENTICATED : 0);
}

#ifndef SPRF_BOND_HOST_TEST
bool sprf_bond_load(SprfBondStoreValues *values) {
  SMPairingInfo info;
  uint8_t flags = 0;

  shared_prf_storage_init();
  memset(values, 0, sizeof(*values));
  if (!shared_prf_storage_get_ble_pairing_data(&info, NULL, NULL, &flags)) {
    return false;
  }
  sprf_bond_from_info(&info, flags, values);
  return values->our_sec_present || values->peer_sec_present;
}

void sprf_bond_save(const struct ble_store_value_sec *our,
                    const struct ble_store_value_sec *peer) {
  SMPairingInfo info;
  uint8_t flags;

  sprf_bond_to_info(our, peer, &info, &flags);
  if (!info.is_remote_identity_info_valid) {
    return;
  }
  shared_prf_storage_store_ble_pairing_data(&info, NULL, false, flags);
}

void sprf_bond_erase(void) { shared_prf_storage_erase_ble_pairing_data(); }
#endif
