/* SPDX-License-Identifier: Apache-2.0 */

#include "sprf_bond.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void prv_fill_key(uint8_t *record, size_t offset, uint8_t first) {
  for (size_t i = 0; i < 16; ++i) {
    record[offset + i] = first + i;
  }
}

int main(void) {
  uint8_t record[sizeof(SprfBlePairingData)] = {0};
  SprfBlePairingData pairing;
  SprfBondStoreValues values;

  prv_fill_key(record, offsetof(SprfBlePairingData, l_ltk), 0x10);
  static const uint8_t local_rand[] = {0x11, 0x22, 0x33, 0x44,
                                       0x55, 0x66, 0x77, 0x88};
  memcpy(record + offsetof(SprfBlePairingData, l_rand), local_rand,
         sizeof(local_rand));
  record[offsetof(SprfBlePairingData, l_ediv)] = 0x34;
  record[offsetof(SprfBlePairingData, l_ediv) + 1] = 0x12;

  record[offsetof(SprfBlePairingData, r_ediv)] = 0xcd;
  record[offsetof(SprfBlePairingData, r_ediv) + 1] = 0xab;
  prv_fill_key(record, offsetof(SprfBlePairingData, r_ltk), 0x80);
  static const uint8_t remote_rand[] = {0xa1, 0xa2, 0xa3, 0xa4,
                                        0xa5, 0xa6, 0xa7, 0xa8};
  memcpy(record + offsetof(SprfBlePairingData, r_rand), remote_rand,
         sizeof(remote_rand));
  prv_fill_key(record, offsetof(SprfBlePairingData, irk), 0x40);

  static const uint8_t peer_address[] = {0x01, 0x23, 0x45,
                                         0x67, 0x89, 0xab};
  const size_t identity_offset = offsetof(SprfBlePairingData, identity);
  memcpy(record + identity_offset, peer_address, sizeof(peer_address));
  record[identity_offset + sizeof(peer_address)] = 0x02;

  memcpy(&pairing, record, sizeof(pairing));
  pairing.fields = SprfValidFields_LocalEncryptionInfoValid |
                   SprfValidFields_RemoteEncryptionInfoValid |
                   SprfValidFields_RemoteIdentityInfoValid;
  pairing.flags = 0x03;

  sprf_bond_map_pairing(&pairing, &values);

  assert(values.our_sec_present);
  assert(values.peer_sec_present);
  assert(values.our_sec.peer_addr.type == BLE_ADDR_RANDOM);
  assert(memcmp(values.our_sec.peer_addr.val, peer_address,
                sizeof(peer_address)) == 0);
  assert(values.our_sec.ediv == 0x1234);
  assert(values.our_sec.rand_num == UINT64_C(0x8877665544332211));
  assert(values.our_sec.ltk_present);
  assert(memcmp(values.our_sec.ltk,
                record + offsetof(SprfBlePairingData, l_ltk), 16) == 0);

  assert(values.peer_sec.ediv == 0xabcd);
  assert(values.peer_sec.rand_num == UINT64_C(0xa8a7a6a5a4a3a2a1));
  assert(values.peer_sec.ltk_present);
  assert(memcmp(values.peer_sec.ltk,
                record + offsetof(SprfBlePairingData, r_ltk), 16) == 0);
  assert(values.peer_sec.irk_present);
  assert(memcmp(values.peer_sec.irk,
                record + offsetof(SprfBlePairingData, irk), 16) == 0);
  assert(values.our_sec.sc && values.our_sec.authenticated);
  assert(values.peer_sec.sc && values.peer_sec.authenticated);

  pairing.fields = SprfValidFields_RemoteIdentityInfoValid;
  sprf_bond_map_pairing(&pairing, &values);
  assert(!values.our_sec_present);
  assert(values.peer_sec_present);
  assert(!values.peer_sec.ltk_present);
  assert(values.peer_sec.irk_present);

  puts("SPRF bond mapping self-check passed");
  return 0;
}
