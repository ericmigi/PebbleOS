/* SPDX-License-Identifier: Apache-2.0 */

/* Kept separate from main.c: zephyr/drivers/display.h and the Pebble applib
 * headers both define display_clear/sign_extend.
 */

#include <stddef.h>
#include <stdint.h>

#include <zephyr/devicetree.h>
#include <zephyr/sys/printk.h>

#if defined(CONFIG_DISPLAY) && DT_HAS_CHOSEN(zephyr_display)

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>

void gfx_port_push_frame(const void *buffer, size_t buf_size, uint16_t width, uint16_t height) {
  const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
  const struct display_buffer_descriptor desc = {
      .buf_size = buf_size,
      .width = width,
      .height = height,
      .pitch = width,
  };

  if (!device_is_ready(display)) {
    printk("GFX_DISPLAY_FAIL %d\n", -ENODEV);
    return;
  }

  int ret = display_blanking_off(display);
  if (ret == 0) {
    ret = display_write(display, 0U, 0U, &desc, buffer);
  }
  if (ret != 0) {
    printk("GFX_DISPLAY_FAIL %d\n", ret);
    return;
  }
  printk("GFX_DISPLAY_PUSH ok\n");
}

#else

void gfx_port_push_frame(const void *buffer, size_t buf_size, uint16_t width, uint16_t height) {
  (void)buffer;
  (void)buf_size;
  (void)width;
  (void)height;
}

#endif
