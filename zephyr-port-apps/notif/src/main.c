/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>

#define display_clear zephyr_display_clear
#define sign_extend zephyr_sign_extend
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/printk.h>
#undef sign_extend
#undef display_clear

#include "applib/graphics/framebuffer.h"
#include "applib/graphics/gcontext.h"
#include "applib/graphics/graphics.h"
#include "applib/ui/layer.h"
#include "notif_port.h"
#include "pbl/services/timeline/attribute.h"
#include "pbl/services/timeline/item.h"
#include "pbl/services/timeline/notification_layout.h"
#include "pbl/util/crc32.h"

static FrameBuffer s_framebuffer;
static GContext s_context;

_Static_assert(PBL_DISPLAY_WIDTH == 200 && PBL_DISPLAY_HEIGHT == 228,
               "pt2 notification framebuffer must be 200x228");

static void prv_background_update_proc(Layer *layer, GContext *ctx) {
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, &layer->bounds);
}

int main(void) {
  static char s_app_name[] = "CoreApp";
  static char s_title[] = "Core Devices";
  static char s_body[] = "Hello from CoreApp over BLE";
  static Attribute s_attributes[] = {
      { .id = AttributeIdAppName, .cstring = s_app_name },
      { .id = AttributeIdSender, .cstring = s_app_name },
      { .id = AttributeIdTitle, .cstring = s_title },
      { .id = AttributeIdBody, .cstring = s_body },
      { .id = AttributeIdIconTiny, .uint32 = TIMELINE_RESOURCE_NOTIFICATION_GENERIC },
  };
  static TimelineItem s_item = {
      .header = {
          .timestamp = 1787587200,
          .type = TimelineItemTypeNotification,
          .visible = true,
          .layout = LayoutIdNotification,
      },
      .attr_list = {
          .num_attributes = ARRAY_SIZE(s_attributes),
          .attributes = s_attributes,
      },
  };

  const GSize display_size = GSize(PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT);
  framebuffer_init(&s_framebuffer, &display_size);
  graphics_context_init(&s_context, &s_framebuffer, GContextInitializationMode_System);
  notif_port_init(&s_context, &s_framebuffer);
  notif_port_fonts_init();

  Layer root;
  const GRect frame = GRect(0, 0, PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT);
  layer_init(&root, &frame);
  layer_set_update_proc(&root, prv_background_update_proc);

  NotificationLayoutInfo layout_info = {
      .item = &s_item,
      .show_notification_timestamp = true,
  };
  const Uuid app_id = UUID_INVALID;
  const LayoutLayerConfig config = {
      .frame = &frame,
      .attributes = &s_item.attr_list,
      .mode = LayoutLayerModeCard,
      .app_id = &app_id,
      .context = &layout_info,
  };
  LayoutLayer *notification = layout_create(s_item.header.layout, &config);
  if (!notification) {
    printk("NOTIF_RENDER_FAIL\n");
    return -ENOMEM;
  }
  layer_add_child(&root, &notification->layer);
  layer_render_tree(&root, &s_context);
  printk("NOTIF_RENDER_OK\n");

  size_t framebuffer_size;
  uint16_t framebuffer_stride;
  uint8_t *framebuffer = notif_port_framebuffer_bytes(&framebuffer_size, &framebuffer_stride);
  const uint32_t frame_crc = crc32(CRC32_INIT, framebuffer, framebuffer_size);
  printk("NOTIF_FRAME 0x%08" PRIx32 "\n", frame_crc);

  const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
  if (!device_is_ready(display)) {
    printk("NOTIF_PUSH_FAIL %d\n", -ENODEV);
    return -ENODEV;
  }
  int ret = display_blanking_off(display);
  if (ret != 0) {
    printk("NOTIF_PUSH_FAIL %d\n", ret);
    return ret;
  }
  const struct display_buffer_descriptor desc = {
      .buf_size = framebuffer_size,
      .width = framebuffer_stride,
      .height = framebuffer_size / framebuffer_stride,
      .pitch = framebuffer_stride,
  };
  // The JDI scan orientation already matches Pebble's native framebuffer.
  ret = display_write(display, 0U, 0U, &desc, framebuffer);
  if (ret != 0) {
    printk("NOTIF_PUSH_FAIL %d\n", ret);
    return ret;
  }
  printk("NOTIF_PUSH_OK\n");
  return 0;
}
