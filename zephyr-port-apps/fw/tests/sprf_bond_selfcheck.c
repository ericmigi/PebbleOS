/* SPDX-License-Identifier: Apache-2.0 */

#include "sprf_bond.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void prv_fill(uint8_t *dst, uint8_t first) {
  for (size_t i = 0; i < 16; ++i) {
    dst[i] = first + i;
  }
}

int main(void) {
  struct ble_store_value_sec our = {0}, peer = {0};
  SMPairingInfo info;
  SprfBondStoreValues values;
  uint8_t flags = 0;

  static const uint8_t peer_address[] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xab};
  peer.peer_addr.type = BLE_ADDR_RANDOM;
  memcpy(peer.peer_addr.val, peer_address, sizeof(peer_address));
  our.peer_addr = peer.peer_addr;
  our.ediv = 0x1234;
  our.rand_num = UINT64_C(0x8877665544332211);
  prv_fill(our.ltk, 0x10);
  our.ltk_present = 1;
  our.sc = our.authenticated = 1;
  peer.ediv = 0xabcd;
  peer.rand_num = UINT64_C(0xa8a7a6a5a4a3a2a1);
  prv_fill(peer.ltk, 0x80);
  peer.ltk_present = 1;
  prv_fill(peer.irk, 0x40);
  peer.irk_present = 1;
  peer.sc = peer.authenticated = 1;

  // store side: NimBLE values -> SMPairingInfo
  sprf_bond_to_info(&our, &peer, &info, &flags);
  assert(flags == 0x03);
  assert(info.is_local_encryption_info_valid && info.is_remote_encryption_info_valid &&
         info.is_remote_identity_info_valid);
  assert(info.identity.is_random_address);
  assert(info.local_encryption_info.ediv == 0x1234);
  assert(info.remote_encryption_info.rand == UINT64_C(0xa8a7a6a5a4a3a2a1));

  // load side: SMPairingInfo -> NimBLE values, must round-trip
  sprf_bond_from_info(&info, flags, &values);
  assert(values.our_sec_present && values.peer_sec_present);
  assert(values.our_sec.peer_addr.type == BLE_ADDR_RANDOM);
  assert(memcmp(values.our_sec.peer_addr.val, peer_address, sizeof(peer_address)) == 0);
  assert(values.our_sec.ediv == 0x1234);
  assert(values.our_sec.rand_num == UINT64_C(0x8877665544332211));
  assert(values.our_sec.ltk_present && memcmp(values.our_sec.ltk, our.ltk, 16) == 0);
  assert(values.peer_sec.ediv == 0xabcd);
  assert(values.peer_sec.rand_num == UINT64_C(0xa8a7a6a5a4a3a2a1));
  assert(values.peer_sec.ltk_present && memcmp(values.peer_sec.ltk, peer.ltk, 16) == 0);
  assert(values.peer_sec.irk_present && memcmp(values.peer_sec.irk, peer.irk, 16) == 0);
  assert(values.our_sec.sc && values.our_sec.authenticated);
  assert(values.peer_sec.sc && values.peer_sec.authenticated);

  // peer-only bond (no local LTK, no remote LTK): identity + IRK survive
  peer.ltk_present = 0;
  sprf_bond_to_info(NULL, &peer, &info, &flags);
  assert(!info.is_local_encryption_info_valid && !info.is_remote_encryption_info_valid);
  sprf_bond_from_info(&info, flags, &values);
  assert(!values.our_sec_present && values.peer_sec_present);
  assert(!values.peer_sec.ltk_present && values.peer_sec.irk_present);

  // no identity at all: nothing to store / load
  sprf_bond_to_info(NULL, NULL, &info, &flags);
  assert(!info.is_remote_identity_info_valid);
  sprf_bond_from_info(&info, flags, &values);
  assert(!values.our_sec_present && !values.peer_sec_present);

  puts("SPRF bond mapping self-check passed");
  return 0;
}
