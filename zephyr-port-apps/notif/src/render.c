/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
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
#include "notif_render.h"
#include "pbl/services/timeline/attribute.h"
#include "pbl/services/timeline/item.h"
#include "pbl/services/timeline/layout_layer.h"
#include "pbl/services/timeline/notification_layout.h"
#include "pbl/services/timeline/timeline_resources.h"
#include "pbl/util/crc32.h"

static FrameBuffer s_framebuffer;
static GContext s_context;
static bool s_inited;

_Static_assert(PBL_DISPLAY_WIDTH == 200 && PBL_DISPLAY_HEIGHT == 228,
               "pt2 notification framebuffer must be 200x228");

static int notif_render_item(TimelineItem *item);

static void prv_background_update_proc(Layer *layer, GContext *ctx) {
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, &layer->bounds);
}

void notif_render_init(void) {
  if (s_inited) {
    return;
  }
  const GSize display_size = GSize(PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT);
  framebuffer_init(&s_framebuffer, &display_size);
  graphics_context_init(&s_context, &s_framebuffer, GContextInitializationMode_System);
  notif_port_init(&s_context, &s_framebuffer);
  notif_port_fonts_init();
  s_inited = true;
}

static int notif_render_item(TimelineItem *item) {
  notif_render_init();

  static Layer root;
  const GRect frame = GRect(0, 0, PBL_DISPLAY_WIDTH, PBL_DISPLAY_HEIGHT);
  layer_init(&root, &frame);
  layer_set_update_proc(&root, prv_background_update_proc);

  NotificationLayoutInfo layout_info = {
      .item = item,
      .show_notification_timestamp = true,
  };
  const Uuid app_id = UUID_INVALID;
  const LayoutLayerConfig config = {
      .frame = &frame,
      .attributes = &item->attr_list,
      .mode = LayoutLayerModeCard,
      .app_id = &app_id,
      .context = &layout_info,
  };
  LayoutLayer *notification = layout_create(item->header.layout, &config);
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
  // Pixels are latched in the framebuffer and the panel; the layout tree (and
  // any strings it referenced) is no longer needed and can be torn down so
  // repeated renders (live blob_db notifications) don't leak.
  layout_destroy(notification);
  return 0;
}

// notif_render_blob_db_value() (Phase 2) is wired in once a real blob_db INSERT
// is observed reaching the watch; it needs the timeline deserialize sources
// (item.c / attributes_actions.c) folded into the build, which is deferred until
// delivery is confirmed so Phase 1 stays a clean, dependency-light render proof.

void notif_render_demo(void) {
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
  notif_render_item(&s_item);
}
