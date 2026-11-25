/* SPDX-FileCopyrightText: 2016-2026 Espressif Systems (Shanghai) CO LTD */
/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "cpu_pll.h"

#include <pbl/kernel/mmio.h>
#include <pbl/util/bits.h>
#include <pbl/util/math.h>

#include <devicetree/regs.h>
#include <hardware/esp32s3_system.h>

#define PLL_POWERDOWN_BITS (PBL_BIT(6) | PBL_BIT(8) | PBL_BIT(10))
#define BBPLL_ANA_CONF0    0x6000e040U
#define BBPLL_CAL_DONE     PBL_BIT(24)
#define RTC_LDO_CONF       (PBL_DT_REG_RTC_CNTL + 0x1fcU)
#define RTC_LDO_SLAVE_PD   PBL_GENMASK(15, 13)
#define EFUSE(offset)      pbl_readl(PBL_DT_REG_EFUSE + (offset))
#define ROM_I2C_WRITE_MASK \
  ((void (*)(uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t))0x40005d6cU)
#define ROM_DELAY_US ((void (*)(uint32_t))0x40000600U)

static int prv_signed_trim(unsigned value, unsigned sign_bit) {
  return (value & sign_bit) != 0 ? -(int)(value & (sign_bit - 1U)) : (int)value;
}

static unsigned prv_bias_for_voltage(int base, int slope, int target) {
  for (int bias = 15; bias < 31; bias++) {
    if (base + slope * (bias - 20) >= target) {
      return (unsigned)bias;
    }
  }
  return 31;
}

// ESP-IDF rtc_init.c: apply the individual chip's PVT calibration, or its nominal bias.
static void prv_set_core_voltage(void) {
  unsigned dig = 28;
  unsigned rtc = 28;
  const unsigned major = EFUSE(0x6cU) & 3U;
  const unsigned minor = (EFUSE(0x50U) >> 24) & 7U;
  const uint32_t low = EFUSE(0x54U);
  const uint32_t high = EFUSE(0x58U);
  const unsigned dig_hvt = (high >> 11) & 31U;
  if ((major > 1 || minor == 1 || (major == 1 && minor >= 2)) && dig_hvt != 0) {
    const int rtc_slope = 198 + prv_signed_trim((low >> 13) & 127U, 64);
    const int dig_slope = 211 + prv_signed_trim((low >> 20) & 127U, 64);
    // Voltages in tenths of a millivolt at bias code 20.
    const int rtc_base = 10181 + 20 * prv_signed_trim((low >> 27) | ((high & 7U) << 5), 128);
    const int dig_base = 10841 + 20 * prv_signed_trim((high >> 3) & 255U, 128);
    const unsigned dig_limit = prv_bias_for_voltage(dig_base, dig_slope, 13000);
    dig = PBL_MIN(dig_hvt + 3, dig_limit);
    const int target = dig_base + dig_slope * ((int)dig - 20) - 250;
    rtc = prv_bias_for_voltage(rtc_base, rtc_slope, target);
  }

  // Internal analog I2C block DIG_REG, EXT_RTC_DREG and EXT_DIG_DREG.
  ROM_I2C_WRITE_MASK(0x6d, 1, 4, 4, 0, rtc);
  ROM_I2C_WRITE_MASK(0x6d, 1, 6, 4, 0, dig);
  ROM_DELAY_US(40);
  pbl_modifyl(RTC_LDO_SLAVE_PD, 0, RTC_LDO_CONF);
}

bool esp32s3_prepare_cpu_pll(void) {
  if ((pbl_readl(PBL_DT_REG_RTC_CNTL) & PLL_POWERDOWN_BITS) != 0U ||
      !(pbl_readl(PBL_DT_REG_SYSTEM + ESP32S3_SYSTEM_CPU_PER_CONF_OFFSET) &
        ESP32S3_SYSTEM_CPU_PER_CONF_PLL_FREQ_SEL) ||
      !(pbl_readl(BBPLL_ANA_CONF0) & BBPLL_CAL_DONE)) {
    return false;
  }
  // USB uses the ROM's calibrated PLL too. Leave its analog configuration running.
  prv_set_core_voltage();
  return true;
}
