/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

struct sd_card {
  uint32_t cid[4];
  uint64_t sectors;
  uint32_t last_status;
  uint8_t last_cmd;
  bool block_addressed;
};

int sd_card_probe(void);
const struct sd_card *sd_card_info(void);
