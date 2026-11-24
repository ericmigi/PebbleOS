/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <inttypes.h>

#include <pbl/devicetree.h>
#include <pbl/drivers/adc/esp32c3.h>
#include <pbl/kernel/delay.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/mmio.h>
#include <pbl/logging/logging.h>
#include <pbl/util/bits.h>
#include <pbl/util/misc.h>

#include <devicetree/regs.h>
#include <hardware/esp32c3_saradc.h>

#define NUM_ATTENS PBL_ADC_ESP32C3__NUM_ATTENS

#define SYSTEM(reg)   (PBL_DT_REG_SYSTEM + ESP32C3_SYSTEM_##reg##_OFFSET)
#define RTC_CNTL(reg) (PBL_DT_REG_RTC_CNTL + ESP32C3_RTC_CNTL_##reg##_OFFSET)

// Controller clock: 80 MHz APB / 16 = 5 MHz, and the SAR clock from it, as ESP-IDF sets them
#define CLKM_DIV_NUM 15U
#define SAR_CLK_DIV  1U
// The start must be held for three controller clocks to be seen: 0.6 us at 5 MHz
#define START_SETUP_US 3U
// A conversion takes some tens of us
#define CONVERT_TIMEOUT_US 1000U

// Reference voltage of each attenuation's calibration point, in microvolts
static const int32_t s_cal_uv[NUM_ATTENS] = {400000, 550000, 750000, 1370000};
// Nominal code at that voltage, for a part without the calibration
#define NOMINAL_CAL_RAW 2000

PBL_LOG_MODULE_DECLARE(driver_adc, CONFIG_DRIVER_ADC_LOG_LEVEL);

typedef void (*prv_rom_i2c_write_t)(uint8_t block, uint8_t host, uint8_t reg, uint8_t msb,
                                    uint8_t lsb, uint8_t data);
typedef uint8_t (*prv_rom_i2c_read_t)(uint8_t block, uint8_t host, uint8_t reg, uint8_t msb,
                                      uint8_t lsb);

static const struct pbl_adc_esp32c3 *prv_esp(const struct pbl_adc *adc) {
  return PBL_CONTAINER_OF(adc, const struct pbl_adc_esp32c3, adc);
}

static void prv_regi2c_write(uint8_t reg, uint8_t msb, uint8_t lsb, uint8_t data) {
  const prv_rom_i2c_write_t write = (prv_rom_i2c_write_t)ESP32C3_ROM_I2C_WRITE_REG_MASK;

  pbl_irq_lock();
  write(ESP32C3_REGI2C_SAR_ADC, ESP32C3_REGI2C_SAR_ADC_HOSTID, reg, msb, lsb, data);
  pbl_irq_unlock();
}

static uint8_t prv_regi2c_read(uint8_t reg, uint8_t msb, uint8_t lsb) {
  const prv_rom_i2c_read_t read = (prv_rom_i2c_read_t)ESP32C3_ROM_I2C_READ_REG_MASK;
  uint8_t data;

  pbl_irq_lock();
  data = read(ESP32C3_REGI2C_SAR_ADC, ESP32C3_REGI2C_SAR_ADC_HOSTID, reg, msb, lsb);
  pbl_irq_unlock();

  return data;
}

static void prv_set_init_code(uint16_t code) {
  prv_regi2c_write(REGI2C_SAR1_INITIAL_CODE_HIGH, 3U, 0U, (uint8_t)(code >> 8));
  prv_regi2c_write(REGI2C_SAR1_INITIAL_CODE_LOW, 7U, 0U, (uint8_t)code);
}

// A field of eFuse BLOCK2, by its first bit and width, up to 32 bits
static uint32_t prv_efuse_field(uint32_t bit, uint32_t len) {
  uintptr_t word = ESP32C3_EFUSE_RD_SYS_PART1_DATA0 + 4U * (bit / 32U);
  uint64_t value = pbl_readl(word);

  if ((bit % 32U) + len > 32U) {
    value |= (uint64_t)pbl_readl(word + 4U) << 32;
  }

  return (uint32_t)(value >> (bit % 32U)) & (uint32_t)PBL_GENMASK(len - 1U, 0U);
}

static void prv_load_calibration(struct pbl_adc_esp32c3_state *st) {
  st->calibrated = prv_efuse_field(EFUSE_BLK_VERSION_MAJOR_BIT, EFUSE_BLK_VERSION_MAJOR_LEN) ==
                   EFUSE_BLK_VERSION_CALIBRATED;

  for (uint32_t atten = 0U; atten < NUM_ATTENS; atten++) {
    if (!st->calibrated) {
      st->cal_raw[atten] = NOMINAL_CAL_RAW;
      continue;
    }

    uint32_t vol = prv_efuse_field(EFUSE_ADC1_CAL_VOL_BIT(atten), EFUSE_ADC1_CAL_LEN);
    int32_t offset = (int32_t)(vol & PBL_GENMASK(8, 0));

    st->init_code[atten] =
        (uint16_t)(1000U + prv_efuse_field(EFUSE_ADC1_INIT_CODE_BIT(atten), EFUSE_ADC1_CAL_LEN));
    st->cal_raw[atten] = 2000 + ((vol & PBL_BIT(9)) != 0U ? -offset : offset);
    PBL_LOG_DBG("atten %" PRIu32 ": init code %u, %" PRId32 " at %" PRId32 " uV", atten,
                st->init_code[atten], st->cal_raw[atten], s_cal_uv[atten]);
  }
}

static int prv_read(const struct pbl_adc *adc, uint32_t channel, int32_t *microvolt) {
  const struct pbl_adc_esp32c3 *esp = prv_esp(adc);
  const struct pbl_adc_esp32c3_state *st = esp->state;
  const uintptr_t onetime = esp->base + ESP32C3_SARADC_ONETIME_SAMPLE_OFFSET;
  uint8_t atten = esp->atten[channel];
  uint32_t sample = PBL_FIELD_PREP(SARADC_ONETIME_CHANNEL_MASK, channel) |
                    PBL_FIELD_PREP(SARADC_ONETIME_ATTEN_MASK, atten);
  int32_t raw;
  int ret;

  pbl_modifyl(SARADC_CTRL_XPD_SAR_FORCE_MASK,
              PBL_FIELD_PREP(SARADC_CTRL_XPD_SAR_FORCE_MASK, SARADC_XPD_SAR_FORCE_ON),
              esp->base + ESP32C3_SARADC_CTRL_OFFSET);
  // The offset code depends on the attenuation, as ESP-IDF sets it for every conversion
  if (st->calibrated) {
    prv_set_init_code(st->init_code[atten]);
  }

  pbl_writel(sample, onetime);
  pbl_writel(SARADC_INT_ADC1_DONE, esp->base + ESP32C3_SARADC_INT_CLR_OFFSET);
  pbl_writel(sample | SARADC_ONETIME_ADC1_SAMPLE, onetime);
  pbl_busy_wait_us(START_SETUP_US);
  pbl_writel(sample | SARADC_ONETIME_ADC1_SAMPLE | SARADC_ONETIME_START, onetime);

  ret = PBL_POLL_BUSY_US(
      (pbl_readl(esp->base + ESP32C3_SARADC_INT_RAW_OFFSET) & SARADC_INT_ADC1_DONE) != 0U, 1U,
      CONVERT_TIMEOUT_US);
  raw = (int32_t)(pbl_readl(esp->base + ESP32C3_SARADC_1_DATA_STATUS_OFFSET) & SARADC_DATA_MASK);

  pbl_writel(sample, onetime);
  pbl_writel(SARADC_INT_ADC1_DONE, esp->base + ESP32C3_SARADC_INT_CLR_OFFSET);
  pbl_modifyl(SARADC_CTRL_XPD_SAR_FORCE_MASK,
              PBL_FIELD_PREP(SARADC_CTRL_XPD_SAR_FORCE_MASK, SARADC_XPD_SAR_FORCE_FSM),
              esp->base + ESP32C3_SARADC_CTRL_OFFSET);

  if (ret != 0) {
    return ret;
  }

  // ESP-IDF's first-step line through the origin; its polynomial second step is not applied
  *microvolt = (int32_t)((int64_t)raw * s_cal_uv[atten] / st->cal_raw[atten]);

  return 0;
}

static int prv_init(const struct pbl_adc *adc) {
  const struct pbl_adc_esp32c3 *esp = prv_esp(adc);
  int ret;

  for (uint8_t i = 0U; i < PBL_ADC_ESP32C3__NUM_CHANNELS; i++) {
    if (esp->atten[i] >= NUM_ATTENS) {
      return -EINVAL;
    }
  }

  // Each channel's pad: no pulls, no input buffer
  ret = pbl_pinctrl_select_state(&esp->pinctrl, PBL_PINCTRL_STATE_DEFAULT);
  if (ret != 0 && ret != -ENOENT) {
    return ret;
  }

  pbl_irq_lock();
  pbl_modifyl(0U, SYSTEM_PERIP_APB_SARADC, SYSTEM(PERIP_CLK_EN0));
  pbl_modifyl(0U, SYSTEM_PERIP_APB_SARADC, SYSTEM(PERIP_RST_EN0));
  pbl_modifyl(SYSTEM_PERIP_APB_SARADC, 0U, SYSTEM(PERIP_RST_EN0));
  pbl_irq_unlock();

  // Out of reset the version reads back; where the block is missing, e.g. QEMU, it reads 0 and
  // the ROM's internal I2C routines may wait forever
  if (pbl_readl(esp->base + ESP32C3_SARADC_DATE_OFFSET) == 0U) {
    return -ENODEV;
  }

  // The SAR's analog registers on the internal I2C bus
  pbl_irq_lock();
  pbl_modifyl(0U, RTC_CNTL_ANA_CONF_SAR_I2C_PU, RTC_CNTL(ANA_CONF));
  pbl_modifyl(I2C_ANA_CONFIG_SAR_FORCE_PD, 0U, ESP32C3_I2C_ANA_CONFIG);
  pbl_modifyl(0U, I2C_ANA_CONFIG2_SAR_FORCE_PU, ESP32C3_I2C_ANA_CONFIG2);
  pbl_irq_unlock();

  pbl_writel(PBL_FIELD_PREP(SARADC_CLKM_CLK_SEL_MASK, SARADC_CLKM_CLK_SEL_APB) |
                 SARADC_CLKM_CLK_EN | PBL_FIELD_PREP(SARADC_CLKM_DIV_B_MASK, 1U) |
                 PBL_FIELD_PREP(SARADC_CLKM_DIV_NUM_MASK, CLKM_DIV_NUM),
             esp->base + ESP32C3_SARADC_CLKM_CONF_OFFSET);
  pbl_modifyl(SARADC_CTRL_XPD_SAR_FORCE_MASK | SARADC_CTRL_SAR_CLK_DIV_MASK | SARADC_CTRL_START |
                  SARADC_CTRL_START_FORCE,
              PBL_FIELD_PREP(SARADC_CTRL_XPD_SAR_FORCE_MASK, SARADC_XPD_SAR_FORCE_FSM) |
                  PBL_FIELD_PREP(SARADC_CTRL_SAR_CLK_DIV_MASK, SAR_CLK_DIV) |
                  SARADC_CTRL_SAR_CLK_GATED,
              esp->base + ESP32C3_SARADC_CTRL_OFFSET);

  prv_load_calibration(esp->state);
  prv_regi2c_write(REGI2C_SAR1_DREF, 6U, 4U, 1U);

  if (!esp->state->calibrated) {
    PBL_LOG_WRN("No eFuse calibration, nominal transfer");
    return 0;
  }

  prv_set_init_code(esp->state->init_code[0]);
  PBL_LOG_DBG("init code reads back %u",
              ((unsigned int)prv_regi2c_read(REGI2C_SAR1_INITIAL_CODE_HIGH, 3U, 0U) << 8) |
                  prv_regi2c_read(REGI2C_SAR1_INITIAL_CODE_LOW, 7U, 0U));

  return 0;
}

const struct pbl_adc_ops pbl_adc_esp32c3_ops = {
  .init = prv_init,
  .read = prv_read,
};

#include PBL_DT_INSTANCES(esp_esp32c3_adc)
