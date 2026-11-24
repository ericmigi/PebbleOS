/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/util/bits.h>

// APB_SARADC, the digital controller of the SAR ADCs, here only for one-shot conversions of ADC1

#define ESP32C3_SARADC_CTRL_OFFSET           0x0000
#define ESP32C3_SARADC_ONETIME_SAMPLE_OFFSET 0x0020
#define ESP32C3_SARADC_1_DATA_STATUS_OFFSET  0x002c
#define ESP32C3_SARADC_INT_RAW_OFFSET        0x0044
#define ESP32C3_SARADC_INT_CLR_OFFSET        0x004c
#define ESP32C3_SARADC_CLKM_CONF_OFFSET      0x0054
#define ESP32C3_SARADC_DATE_OFFSET           0x03fc

// SAR power: 3 forces it on, 0 leaves it to the controller's state machine
#define SARADC_CTRL_XPD_SAR_FORCE_MASK PBL_GENMASK(28, 27)
#define SARADC_XPD_SAR_FORCE_ON        3U
#define SARADC_XPD_SAR_FORCE_FSM       0U
#define SARADC_CTRL_SAR_CLK_DIV_MASK   PBL_GENMASK(14, 7)
#define SARADC_CTRL_SAR_CLK_GATED      PBL_BIT(6)
// The pattern controller, which must stay stopped: running, it biases conversions low
#define SARADC_CTRL_START       PBL_BIT(1)
#define SARADC_CTRL_START_FORCE PBL_BIT(0)

#define SARADC_ONETIME_ADC1_SAMPLE  PBL_BIT(31)
#define SARADC_ONETIME_ADC2_SAMPLE  PBL_BIT(30)
#define SARADC_ONETIME_START        PBL_BIT(29)
#define SARADC_ONETIME_CHANNEL_MASK PBL_GENMASK(28, 25)
#define SARADC_ONETIME_ATTEN_MASK   PBL_GENMASK(24, 23)

#define SARADC_DATA_MASK PBL_GENMASK(11, 0)

#define SARADC_INT_ADC1_DONE PBL_BIT(31)

// Controller clock: source 2 is the APB clock, divided by DIV_NUM + 1
#define SARADC_CLKM_CLK_SEL_MASK PBL_GENMASK(22, 21)
#define SARADC_CLKM_CLK_SEL_APB  2U
#define SARADC_CLKM_CLK_EN       PBL_BIT(20)
#define SARADC_CLKM_DIV_A_MASK   PBL_GENMASK(19, 14)
#define SARADC_CLKM_DIV_B_MASK   PBL_GENMASK(13, 8)
#define SARADC_CLKM_DIV_NUM_MASK PBL_GENMASK(7, 0)

// SYSTEM: the controller's bus clock and reset
#define ESP32C3_SYSTEM_PERIP_CLK_EN0_OFFSET 0x0010
#define ESP32C3_SYSTEM_PERIP_RST_EN0_OFFSET 0x0018
#define SYSTEM_PERIP_APB_SARADC             PBL_BIT(28)

// RTC_CNTL: power of the analog registers' internal I2C bus, on from reset
#define ESP32C3_RTC_CNTL_ANA_CONF_OFFSET 0x0034
#define RTC_CNTL_ANA_CONF_SAR_I2C_PU     PBL_BIT(22)

// The internal I2C master's analog configuration: the SAR's registers forced off or on
#define ESP32C3_I2C_ANA_CONFIG       0x6000e044U
#define ESP32C3_I2C_ANA_CONFIG2      0x6000e048U
#define I2C_ANA_CONFIG_SAR_FORCE_PD  PBL_BIT(18)
#define I2C_ANA_CONFIG2_SAR_FORCE_PU PBL_BIT(16)

// The SAR's analog registers, block 0x69 on the internal I2C bus
#define ESP32C3_REGI2C_SAR_ADC        0x69U
#define ESP32C3_REGI2C_SAR_ADC_HOSTID 0U
// ADC1 offset code, 12 bits over two registers
#define REGI2C_SAR1_INITIAL_CODE_HIGH 0x1U
#define REGI2C_SAR1_INITIAL_CODE_LOW  0x0U
// ADC1 reference, bits 6:4
#define REGI2C_SAR1_DREF 0x2U

/*
 * ROM routines for the internal I2C bus, at the addresses of ESP-IDF's
 * components/esp_rom/esp32c3/ld/esp32c3.rom.ld (rom_i2c_readReg_Mask, rom_i2c_writeReg_Mask).
 */
#define ESP32C3_ROM_I2C_READ_REG_MASK  0x40001958U
#define ESP32C3_ROM_I2C_WRITE_REG_MASK 0x40001960U

// eFuse BLOCK2 (SYS_DATA_PART1), as read at boot: eight words from here
#define ESP32C3_EFUSE_RD_SYS_PART1_DATA0 0x6000885cU

// Fields of BLOCK2, by their first bit and width
#define EFUSE_BLK_VERSION_MAJOR_BIT 128U
#define EFUSE_BLK_VERSION_MAJOR_LEN 2U
// The block version with the ADC calibration
#define EFUSE_BLK_VERSION_CALIBRATED 1U
// ADC1 offset code at each attenuation, less 1000
#define EFUSE_ADC1_INIT_CODE_BIT(atten) (148U + 10U * (atten))
// ADC1 code at a reference voltage at each attenuation, as a sign bit and an offset from 2000
#define EFUSE_ADC1_CAL_VOL_BIT(atten) (188U + 10U * (atten))
#define EFUSE_ADC1_CAL_LEN            10U
