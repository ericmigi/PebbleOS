/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stdint.h>

#include <pbl/drivers/mcu.h>
#include <pbl/kernel/mmio.h>

#include <devicetree/regs.h>

// The factory MAC address in eFuse block 1, which the controller mirrors at boot, its last octet
// in the lowest byte
#define EFUSE_RD_MAC_SPI_SYS_0_OFFSET 0x0044
#define EFUSE_RD_MAC_SPI_SYS_1_OFFSET 0x0048

#define MAC_LEN 6U

int mcu_get_serial(void *buf, size_t *buf_sz) {
  const uint32_t lo = pbl_readl(PBL_DT_REG_EFUSE + EFUSE_RD_MAC_SPI_SYS_0_OFFSET);
  const uint32_t hi = pbl_readl(PBL_DT_REG_EFUSE + EFUSE_RD_MAC_SPI_SYS_1_OFFSET);
  uint8_t *mac = buf;

  if (*buf_sz < MAC_LEN) {
    return -ENOSPC;
  }

  // In the order it is written, 08:92:72:c5:bc:64 for example
  mac[0] = (uint8_t)(hi >> 8);
  mac[1] = (uint8_t)hi;
  mac[2] = (uint8_t)(lo >> 24);
  mac[3] = (uint8_t)(lo >> 16);
  mac[4] = (uint8_t)(lo >> 8);
  mac[5] = (uint8_t)lo;
  *buf_sz = MAC_LEN;

  return 0;
}
