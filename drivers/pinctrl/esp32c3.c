/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>

#include <pbl/devicetree.h>
#include <pbl/drivers/pinctrl/esp32c3.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/mmio.h>
#include <pbl/util/bits.h>
#include <pbl/util/misc.h>
#include <pbl/util/size.h>

#include <hardware/esp32c3_gpio.h>

static const struct pbl_pinctrl_esp32c3 *prv_ctrl(const struct pbl_pinctrl_controller *ctrl) {
  return PBL_CONTAINER_OF(ctrl, const struct pbl_pinctrl_esp32c3, ctrl);
}

static int prv_configure_pin(const struct pbl_pinctrl_esp32c3 *ctrl, uint32_t pin) {
  uint32_t pad = PBL_FIELD_GET(PBL_PINCTRL_ESP32C3_PAD_MSK, pin);
  uint32_t in = PBL_FIELD_GET(PBL_PINCTRL_ESP32C3_IN_MSK, pin);
  uint32_t out = PBL_FIELD_GET(PBL_PINCTRL_ESP32C3_OUT_MSK, pin);
  uint32_t mask = IO_MUX_FUN_PD | IO_MUX_FUN_PU | IO_MUX_FUN_IE | IO_MUX_MCU_SEL_MASK;
  uint32_t iomux = PBL_FIELD_PREP(IO_MUX_MCU_SEL_MASK, IO_MUX_FUNC_GPIO);

  if (pad >= ESP32C3_GPIO_NUM_PINS) {
    return -EINVAL;
  }
  if (ESP32C3_GPIO_IS_USB(pad)) {
    return -EBUSY;
  }

  if ((pin & PBL_PINCTRL_ESP32C3_PULL_UP) != 0U) {
    iomux |= IO_MUX_FUN_PU;
  }
  if ((pin & PBL_PINCTRL_ESP32C3_PULL_DOWN) != 0U) {
    iomux |= IO_MUX_FUN_PD;
  }
  if ((pin & PBL_PINCTRL_ESP32C3_DRIVE_SET) != 0U) {
    mask |= IO_MUX_FUN_DRV_MASK;
    iomux |= PBL_FIELD_PREP(IO_MUX_FUN_DRV_MASK, PBL_FIELD_GET(PBL_PINCTRL_ESP32C3_DRIVE_MSK, pin));
  }
  // A GPIO output reads back, as with the GPIO driver
  if (in != ESP32C3_NOSIG || out == ESP32C3_SIG_GPIO) {
    iomux |= IO_MUX_FUN_IE;
  }

  pbl_irq_lock();
  if (out != ESP32C3_NOSIG) {
    pbl_modifyl(GPIO_PIN_PAD_DRIVER,
                (pin & PBL_PINCTRL_ESP32C3_OPEN_DRAIN) != 0U ? GPIO_PIN_PAD_DRIVER : 0U,
                ctrl->matrix + ESP32C3_GPIO_PIN_OFFSET(pad));
    // A GPIO keeps its enable in the matrix's own register, a peripheral gives its own
    pbl_writel(out | (out == ESP32C3_SIG_GPIO ? GPIO_FUNC_OUT_OEN_SEL : 0U),
               ctrl->matrix + ESP32C3_GPIO_FUNC_OUT_SEL_CFG_OFFSET(pad));
  }
  if (in != ESP32C3_NOSIG) {
    pbl_writel(pad | GPIO_FUNC_IN_SEL, ctrl->matrix + ESP32C3_GPIO_FUNC_IN_SEL_CFG_OFFSET(in));
  }
  pbl_modifyl(mask, iomux, ctrl->base + ESP32C3_IO_MUX_GPIO_OFFSET(pad));
  pbl_irq_unlock();

  return 0;
}

static int prv_configure(const struct pbl_pinctrl_controller *ctrl, const uint32_t *pins,
                         uint8_t num_pins, uintptr_t reg) {
  for (uint8_t i = 0U; i < num_pins; i++) {
    int ret = prv_configure_pin(prv_ctrl(ctrl), pins[i]);

    if (ret != 0) {
      return ret;
    }
  }

  return 0;
}

static int prv_get(const struct pbl_pinctrl_controller *ctrl, uint16_t pad, uint32_t *val) {
  if (pad >= ESP32C3_GPIO_NUM_PINS) {
    return -EINVAL;
  }

  *val = pbl_readl(prv_ctrl(ctrl)->base + ESP32C3_IO_MUX_GPIO_OFFSET(pad));

  return 0;
}

const struct pbl_pinctrl_controller_ops pbl_pinctrl_esp32c3_ops = {
  .configure = prv_configure,
  .get = prv_get,
};

#define PBL_PINCTRL_ESP32C3_DT_STATE(sym, ...)        \
  static const uint32_t sym##_pins[] = {__VA_ARGS__}; \
  const struct pbl_pinctrl_state sym = {              \
    .ctrl = ESP32C3_PINCTRL,                          \
    .pins = sym##_pins,                               \
    .num_pins = PBL_ARRAY_SIZE(sym##_pins),           \
  }

#include PBL_DT_INSTANCES(esp_esp32c3_pinctrl_pinctrl_controller)
#include PBL_DT_INSTANCES(esp_esp32c3_pinctrl_pinctrl)
