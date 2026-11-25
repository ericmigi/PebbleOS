/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>

#include <pbl/devicetree.h>
#include <pbl/drivers/gpio/esp32c3.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/mmio.h>
#include <pbl/util/bits.h>
#include <pbl/util/misc.h>

#include <hardware/esp32c3_gpio.h>

#define PAD_MASK (IO_MUX_FUN_PD | IO_MUX_FUN_PU | IO_MUX_FUN_IE | IO_MUX_MCU_SEL_MASK)

static const struct pbl_gpio_esp32c3 *prv_port(const struct pbl_gpio_port *port) {
  return PBL_CONTAINER_OF(port, const struct pbl_gpio_esp32c3, port);
}

static const struct pbl_gpio_esp32c3 *prv_intc_port(const struct pbl_intc *intc) {
  return PBL_CONTAINER_OF(intc, const struct pbl_gpio_esp32c3_intc, intc)->port;
}

static inline void prv_putreg(const struct pbl_gpio_esp32c3 *gpio, uint32_t offset,
                              uint32_t value) {
  pbl_writel(value, gpio->base + offset);
}

static inline void prv_bank_write(const struct pbl_gpio_esp32c3 *gpio, uint32_t off0, uint32_t off1,
                                  uint8_t pin) {
  if (pin < 32U) {
    prv_putreg(gpio, off0, PBL_BIT(pin));
  } else {
    prv_putreg(gpio, off1, PBL_BIT(pin - 32U));
  }
}

// The level, the matrix and the pad come before the enable: an output starts at its level
static int prv_configure(const struct pbl_gpio_port *port, uint8_t pin, uint32_t flags) {
  const struct pbl_gpio_esp32c3 *gpio = prv_port(port);
  uint32_t pad = IO_MUX_FUN_IE | PBL_FIELD_PREP(IO_MUX_MCU_SEL_MASK, IO_MUX_FUNC_GPIO);

  if (!esp32_gpio_pad_available(pin) || (flags & (PBL_GPIO_INPUT | PBL_GPIO_OUTPUT)) == 0U) {
    return -EINVAL;
  }
  if (ESP32C3_GPIO_IS_USB(pin)) {
    return -EBUSY;
  }

  if ((flags & PBL_GPIO_PULL_UP) != 0U) {
    pad |= IO_MUX_FUN_PU;
  }
  if ((flags & PBL_GPIO_PULL_DOWN) != 0U) {
    pad |= IO_MUX_FUN_PD;
  }

  pbl_irq_lock();
  if ((flags & PBL_GPIO_OUTPUT) != 0U) {
    if ((flags & PBL_GPIO_OUTPUT_INIT_HIGH) != 0U) {
      prv_bank_write(gpio, ESP32C3_GPIO_OUT_W1TS_OFFSET, ESP32C3_GPIO_OUT1_W1TS_OFFSET, pin);
    } else if ((flags & PBL_GPIO_OUTPUT_INIT_LOW) != 0U) {
      prv_bank_write(gpio, ESP32C3_GPIO_OUT_W1TC_OFFSET, ESP32C3_GPIO_OUT1_W1TC_OFFSET, pin);
    }
    pbl_modifyl(GPIO_PIN_PAD_DRIVER, (flags & PBL_GPIO_OPEN_DRAIN) != 0U ? GPIO_PIN_PAD_DRIVER : 0U,
                gpio->base + ESP32C3_GPIO_PIN_OFFSET(pin));
    prv_putreg(gpio, ESP32C3_GPIO_FUNC_OUT_SEL_CFG_OFFSET(pin), GPIO_FUNC_OUT_GPIO);
    pbl_modifyl(PAD_MASK, pad, gpio->io_mux + ESP32C3_IO_MUX_GPIO_OFFSET(pin));
    prv_bank_write(gpio, ESP32C3_GPIO_ENABLE_W1TS_OFFSET, ESP32C3_GPIO_ENABLE1_W1TS_OFFSET, pin);
  } else {
    prv_bank_write(gpio, ESP32C3_GPIO_ENABLE_W1TC_OFFSET, ESP32C3_GPIO_ENABLE1_W1TC_OFFSET, pin);
    pbl_modifyl(PAD_MASK, pad, gpio->io_mux + ESP32C3_IO_MUX_GPIO_OFFSET(pin));
  }
  pbl_irq_unlock();

  return 0;
}

static int prv_get(const struct pbl_gpio_port *port, uint8_t pin) {
  const struct pbl_gpio_esp32c3 *gpio = prv_port(port);

  if (!esp32_gpio_pad_available(pin)) {
    return -EINVAL;
  }

  if (pin < 32U) {
    return (pbl_readl(gpio->base + ESP32C3_GPIO_IN_OFFSET) & PBL_BIT(pin)) != 0U;
  }
  return (pbl_readl(gpio->base + ESP32C3_GPIO_IN1_OFFSET) & PBL_BIT(pin - 32U)) != 0U;
}

static int prv_set(const struct pbl_gpio_port *port, uint8_t pin, bool level) {
  const struct pbl_gpio_esp32c3 *gpio = prv_port(port);

  if (!esp32_gpio_pad_available(pin)) {
    return -EINVAL;
  }

  if (level) {
    prv_bank_write(gpio, ESP32C3_GPIO_OUT_W1TS_OFFSET, ESP32C3_GPIO_OUT1_W1TS_OFFSET, pin);
  } else {
    prv_bank_write(gpio, ESP32C3_GPIO_OUT_W1TC_OFFSET, ESP32C3_GPIO_OUT1_W1TC_OFFSET, pin);
  }

  return 0;
}

const struct pbl_gpio_port_ops pbl_gpio_esp32c3_ops = {
  .configure = prv_configure,
  .get = prv_get,
  .set = prv_set,
};

static int prv_intc_configure(const struct pbl_intc *intc, uint32_t hwirq, uint32_t type) {
  const struct pbl_gpio_esp32c3 *gpio = prv_intc_port(intc);
  uint32_t hw;

  if (!esp32_gpio_pad_available(hwirq)) {
    return -EINVAL;
  }

  switch (type) {
    case PBL_IRQ_TYPE_EDGE_RISING:
      hw = GPIO_INT_TYPE_RISING;
      break;
    case PBL_IRQ_TYPE_EDGE_FALLING:
      hw = GPIO_INT_TYPE_FALLING;
      break;
    case PBL_IRQ_TYPE_EDGE_BOTH:
      hw = GPIO_INT_TYPE_ANY;
      break;
    case PBL_IRQ_TYPE_LEVEL_HIGH:
      hw = GPIO_INT_TYPE_HIGH;
      break;
    case PBL_IRQ_TYPE_LEVEL_LOW:
      hw = GPIO_INT_TYPE_LOW;
      break;
    default:
      return -ENOTSUP;
  }

  pbl_modifyl(GPIO_PIN_INT_TYPE_MASK, PBL_FIELD_PREP(GPIO_PIN_INT_TYPE_MASK, hw),
              gpio->base + ESP32C3_GPIO_PIN_OFFSET(hwirq));

  return 0;
}

static int prv_intc_enable(const struct pbl_intc *intc, uint32_t hwirq) {
  const struct pbl_gpio_esp32c3 *gpio = prv_intc_port(intc);

  pbl_irq_lock();
  prv_bank_write(gpio, ESP32C3_GPIO_STATUS_W1TC_OFFSET, ESP32C3_GPIO_STATUS1_W1TC_OFFSET,
                 (uint8_t)hwirq);
  pbl_modifyl(GPIO_PIN_INT_ENA_MASK, PBL_FIELD_PREP(GPIO_PIN_INT_ENA_MASK, GPIO_PIN_INT_ENA_CPU),
              gpio->base + ESP32C3_GPIO_PIN_OFFSET(hwirq));
  pbl_irq_unlock();

  return 0;
}

static int prv_intc_disable(const struct pbl_intc *intc, uint32_t hwirq) {
  pbl_modifyl(GPIO_PIN_INT_ENA_MASK, 0U,
              prv_intc_port(intc)->base + ESP32C3_GPIO_PIN_OFFSET(hwirq));

  return 0;
}

const struct pbl_intc_ops pbl_gpio_esp32c3_intc_ops = {
  .configure = prv_intc_configure,
  .enable = prv_intc_enable,
  .disable = prv_intc_disable,
};

// A level still there raises the interrupt again
void pbl_gpio_esp32c3_irq_handler(const struct pbl_gpio_esp32c3 *gpio) {
  uint32_t pending = pbl_readl(gpio->base + ESP32C3_GPIO_PCPU_INT_OFFSET);

  prv_putreg(gpio, ESP32C3_GPIO_STATUS_W1TC_OFFSET, pending);
  while (pending != 0U) {
    pbl_intc_dispatch(gpio->port.intc, (uint32_t)PBL_CTZ(pending));
    pending &= pending - 1U;
  }

#ifdef CONFIG_SOC_ESP32S3
  uint32_t pending1 = pbl_readl(gpio->base + ESP32C3_GPIO_PCPU_INT1_OFFSET);

  if (pending1 != 0U) {
    prv_putreg(gpio, ESP32C3_GPIO_STATUS1_W1TC_OFFSET, pending1);
    while (pending1 != 0U) {
      pbl_intc_dispatch(gpio->port.intc, 32U + (uint32_t)PBL_CTZ(pending1));
      pending1 &= pending1 - 1U;
    }
  }
#endif
}

int pbl_gpio_esp32c3_init(const struct pbl_device *dev) {
  const struct pbl_gpio_esp32c3 *gpio =
      PBL_CONTAINER_OF(dev, const struct pbl_gpio_esp32c3, port.dev);

  pbl_irq_enable(gpio->irq);

  return 0;
}

#include PBL_DT_INSTANCES(esp_esp32c3_gpio)
