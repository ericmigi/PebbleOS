/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "sprf_bond.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef SPRF_BOND_HOST_TEST
#include <pbl/drivers/flash.h>
#include <pbl/util/crc32.h>
#include "flash_region/flash_region.h"
#endif

#define SPRF_VERSION 0x02u
#define SPRF_KEY_SIZE 16u
#define SPRF_FLAG_SECURE_CONNECTIONS 0x01u
#define SPRF_FLAG_AUTHENTICATED 0x02u

_Static_assert(offsetof(SharedPRFData, ble_pairing_data) == 44,
               "Unexpected SPRF pairing offset");
_Static_assert(sizeof(SprfBlePairingData) == 100,
               "Unexpected SPRF pairing size");

static void prv_set_peer_address(const SprfBlePairingData *pairing,
                                 struct ble_store_value_sec *value) {
  value->peer_addr.type =
      pairing->identity.is_random_address ? BLE_ADDR_RANDOM : BLE_ADDR_PUBLIC;
  memcpy(value->peer_addr.val, pairing->identity.address.octets,
         sizeof(value->peer_addr.val));
}

static void prv_set_security_flags(const SprfBlePairingData *pairing,
                                   struct ble_store_value_sec *value) {
  value->key_size = SPRF_KEY_SIZE;
  value->sc = !!(pairing->flags & SPRF_FLAG_SECURE_CONNECTIONS);
  value->authenticated = !!(pairing->flags & SPRF_FLAG_AUTHENTICATED);
}

void sprf_bond_map_pairing(const SprfBlePairingData *pairing,
                           SprfBondStoreValues *values) {
  const uint8_t fields = pairing->fields;

  memset(values, 0, sizeof(*values));
  if (!(fields & SprfValidFields_RemoteIdentityInfoValid)) {
    return;
  }

  if (fields & SprfValidFields_LocalEncryptionInfoValid) {
    struct ble_store_value_sec *our = &values->our_sec;

    prv_set_peer_address(pairing, our);
    prv_set_security_flags(pairing, our);
    our->ediv = pairing->l_ediv;
    our->rand_num = pairing->l_rand;
    memcpy(our->ltk, pairing->l_ltk.data, sizeof(our->ltk));
    our->ltk_present = 1;
    values->our_sec_present = true;
  }

  struct ble_store_value_sec *peer = &values->peer_sec;
  prv_set_peer_address(pairing, peer);
  prv_set_security_flags(pairing, peer);

  if (fields & SprfValidFields_RemoteEncryptionInfoValid) {
    peer->ediv = pairing->r_ediv;
    peer->rand_num = pairing->r_rand;
    memcpy(peer->ltk, pairing->r_ltk.data, sizeof(peer->ltk));
    peer->ltk_present = 1;
  }

  memcpy(peer->irk, pairing->irk.data, sizeof(peer->irk));
  peer->irk_present = 1;
  values->peer_sec_present = true;
}

#ifndef SPRF_BOND_HOST_TEST
bool sprf_bond_load(SprfBondStoreValues *values) {
  SharedPRFData data;
  uint32_t stored_crc;

  memset(values, 0, sizeof(*values));
  flash_read_bytes((uint8_t *)&data, FLASH_REGION_SHARED_PRF_STORAGE_BEGIN,
                   sizeof(data));

  if (data.magic != SprfMagic_ValidEntry || data.version != SPRF_VERSION) {
    return false;
  }

  memcpy(&stored_crc, &data.ble_pairing_data.crc, sizeof(stored_crc));
  const uint32_t computed_crc = crc32(
      CRC32_INIT, (const uint8_t *)&data.ble_pairing_data + sizeof(stored_crc),
      sizeof(data.ble_pairing_data) - sizeof(stored_crc));
  if (stored_crc == UINT32_MAX || stored_crc != computed_crc) {
    return false;
  }

  sprf_bond_map_pairing(&data.ble_pairing_data, values);
  return values->our_sec_present || values->peer_sec_present;
}
#endif
