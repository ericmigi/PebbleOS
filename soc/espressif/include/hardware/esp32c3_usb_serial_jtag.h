/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/util/bits.h>

#define ESP32C3_USJ_EP1_OFFSET      0x0000
#define ESP32C3_USJ_EP1_CONF_OFFSET 0x0004
#define ESP32C3_USJ_INT_RAW_OFFSET  0x0008
#define ESP32C3_USJ_INT_ST_OFFSET   0x000c
#define ESP32C3_USJ_INT_ENA_OFFSET  0x0010
#define ESP32C3_USJ_INT_CLR_OFFSET  0x0014

// Hands the bytes written so far to the host as one packet
#define USJ_EP1_CONF_WR_DONE                  PBL_BIT(0)
#define USJ_EP1_CONF_SERIAL_IN_EP_DATA_FREE   PBL_BIT(1)
#define USJ_EP1_CONF_SERIAL_OUT_EP_DATA_AVAIL PBL_BIT(2)

#define USJ_INT_SERIAL_OUT_RECV_PKT PBL_BIT(2)
