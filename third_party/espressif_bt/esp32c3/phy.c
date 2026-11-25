/*
 * SPDX-FileCopyrightText: 2015-2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2026 Core Devices LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// The PHY bring-up and the hooks the PHY library calls, from ESP-IDF 5.5.5
// components/esp_phy/src/{phy_init,phy_common,phy_override,lib_printf}.c and the clock gating of
// components/esp_hw_support/periph_ctrl.c, for Bluetooth alone on the ESP32-C3: no calibration
// data storage (a full calibration at every enable), no PLL tracking (the controller turns it off
// for BLE), no antenna switching, no Wi-Fi.

#include "phy.h"

#include "esp32c3_regs.h"
#include "phy_init_data.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <pbl/kernel/assert.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/malloc.h>
#include <pbl/kernel/mmio.h>
#include <pbl/logging/logging.h>

PBL_LOG_MODULE_DECLARE(espressif_bt, CONFIG_ESP32C3_BT_LOG_LEVEL);

// ROM: ets_delay_us(), calibrated by the SoC init
extern void ets_delay_us(uint32_t us);

/* PHY spinlock for libphy.a: interrupts masked, nestable */

uint32_t phy_enter_critical(void) {
  pbl_irq_lock();
  // Interrupt level will be stored in current tcb, so always return zero.
  return 0;
}

void phy_exit_critical(uint32_t level) {
  pbl_irq_unlock();
}

// Overrides of the library's weak hooks (phy_override.c). The analog I2C bus is the one the SAR ADC
// driver uses, under the same lock.
void phy_i2c_enter_critical(void) {
  pbl_irq_lock();
}

void phy_i2c_exit_critical(void) {
  pbl_irq_unlock();
}

// sar_periph_ctrl_pwdet_power_acquire() and _release(): the power detector forces the SAR ADC on
// through its own register, which the ADC driver does not touch, so a count of its own does
static int s_pwdet_power_on_cnt;

void phy_set_pwdet_power(bool en) {
  pbl_irq_lock();
  if (en) {
    if (s_pwdet_power_on_cnt++ == 0) {
      pbl_modifyl(0, PWDET_SAR_POWER_FORCE | PWDET_SAR_POWER_CNTL, PWDET_CONF_REG);
    }
  } else if (s_pwdet_power_on_cnt > 0 && --s_pwdet_power_on_cnt == 0) {
    pbl_modifyl(PWDET_SAR_POWER_FORCE, 0, PWDET_CONF_REG);
  }
  pbl_irq_unlock();
}

#define VPRINTF_STACK_BUFFER_SIZE 80

int phy_printf(const char *format, ...) {
  char temp[VPRINTF_STACK_BUFFER_SIZE];
  va_list arg;
  int len;
  int i;

  va_start(arg, format);
  len = vsnprintf(temp, sizeof(temp) - 1, format, arg);
  va_end(arg);
  temp[sizeof(temp) - 1] = 0;
  for (i = len - 1; i >= 0; --i) {
    if (i < (int)sizeof(temp) - 1 && temp[i] != '\n' && temp[i] != '\r' && temp[i] != ' ') {
      break;
    }
    temp[i] = 0;
  }
  if (i > 0) {
    PBL_LOG_INFO("phy: %s", temp);
  }
  return len;
}

/* Clock gating (periph_ctrl.c): reference counts of the modules the PHY and controller share */

static uint8_t s_common_ref;
static uint8_t s_rng_ref;
static uint8_t s_bt_ref;

static void wifi_bt_common_module_enable(void) {
  pbl_irq_lock();
  if (s_common_ref++ == 0) {
    pbl_modifyl(0, SYSTEM_WIFI_CLK_WIFI_BT_COMMON_M, SYSTEM_WIFI_CLK_EN_REG);
  }
  pbl_irq_unlock();
}

static void wifi_bt_common_module_disable(void) {
  pbl_irq_lock();
  if (--s_common_ref == 0) {
    pbl_modifyl(SYSTEM_WIFI_CLK_WIFI_BT_COMMON_M, 0, SYSTEM_WIFI_CLK_EN_REG);
  }
  pbl_irq_unlock();
}

// The C3 has no Bluetooth clock bit of its own: enabling the module releases its resets
void periph_bt_module_enable(void) {
  pbl_irq_lock();
  if (s_bt_ref++ == 0) {
    pbl_modifyl(BT_MODULE_RST_MASK, 0, SYSTEM_WIFI_RST_EN_REG);
  }
  pbl_irq_unlock();
}

void periph_bt_module_reset(void) {
  pbl_irq_lock();
  pbl_modifyl(0, BT_MODULE_RST_MASK, SYSTEM_WIFI_RST_EN_REG);
  pbl_modifyl(BT_MODULE_RST_MASK, 0, SYSTEM_WIFI_RST_EN_REG);
  pbl_irq_unlock();
}

static void periph_bt_module_disable(void) {
  pbl_irq_lock();
  if (--s_bt_ref == 0) {
    pbl_modifyl(0, BT_MODULE_RST_MASK, SYSTEM_WIFI_RST_EN_REG);
  }
  pbl_irq_unlock();
}

// PERIPH_WIFI_BT_COMMON_MODULE is enabled outside
static void phy_module_enable(void) {
  // The PHY calibration clock (bit 22) and the RNG's (bit 15); the Wi-Fi module has no bits
  pbl_irq_lock();
  pbl_modifyl(0, SYSTEM_WIFI_CLK_PHY_EN_M, SYSTEM_WIFI_CLK_EN_REG);
  if (s_rng_ref++ == 0) {
    pbl_modifyl(0, PBL_BIT(15), SYSTEM_WIFI_CLK_EN_REG);
  }
  pbl_irq_unlock();
  periph_bt_module_enable();
}

// PERIPH_WIFI_BT_COMMON_MODULE is disabled outside
static void phy_module_disable(void) {
  periph_bt_module_disable();
  // Do not disable PHY clock and RNG clock
  pbl_irq_lock();
  s_rng_ref--;
  pbl_irq_unlock();
}

/* Power (phy_init.c) */

void esp_wifi_bt_power_domain_on(void) {
  static bool s_on;

  if (s_on) {
    return;
  }
  s_on = true;

  pbl_modifyl(RTC_CNTL_WIFI_FORCE_PD, 0, RTC_CNTL_DIG_PWC_REG);
  ets_delay_us(10);
  wifi_bt_common_module_enable();
  // modem reset when power on
  pbl_modifyl(0, MODEM_RESET_FIELD_WHEN_PU, SYSTEM_WIFI_RST_EN_REG);
  pbl_modifyl(MODEM_RESET_FIELD_WHEN_PU, 0, SYSTEM_WIFI_RST_EN_REG);
  pbl_modifyl(RTC_CNTL_WIFI_FORCE_ISO, 0, RTC_CNTL_DIG_ISO_REG);
  wifi_bt_common_module_disable();
}

/* Enable (phy_init.c, phy_common.c) */

static esp_phy_modem_t s_phy_modem_flag;
static bool s_is_phy_calibrated;

static void esp_phy_load_cal_and_init(void) {
  char *phy_version = get_phy_version_str();
  PBL_LOG_INFO("phy_version %s", phy_version);

  esp_phy_calibration_data_t *cal_data = pbl_calloc(1, sizeof(esp_phy_calibration_data_t));
  PBL_ASSERT(cal_data != nullptr, "failed to allocate memory for RF calibration data");

  const esp_phy_init_data_t *init_data = &phy_init_data;

  // Before anything else: without it the PHY init turns the USB PHY off, and with it the
  // USB-Serial-JTAG console, the only way into the board
#if CONFIG_ESP_PHY_ENABLE_USB
  phy_bbpll_en_usb(true);
#endif

  register_chipv7_phy(init_data, cal_data, PHY_RF_CAL_FULL);

  pbl_free(cal_data); // PHY maintains a copy of calibration data, so we can free this
}

void esp_phy_enable(esp_phy_modem_t modem) {
  if (s_phy_modem_flag == 0) {
    wifi_bt_common_module_enable();
    phy_module_enable();
    if (s_is_phy_calibrated == false) {
      esp_phy_load_cal_and_init();
      s_is_phy_calibrated = true;
    } else {
      extern void phy_wakeup_init(void);
      phy_wakeup_init();
    }
    phy_module_disable();
  }
  s_phy_modem_flag |= modem;
}
