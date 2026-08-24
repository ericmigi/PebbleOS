/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/printk.h>

#include "applib/fonts/fonts_private.h"
#include "applib/graphics/framebuffer.h"
#include "applib/graphics/gcontext.h"
#include "applib/graphics/graphics.h"
#include "applib/graphics/text.h"
#include "applib/graphics/text_resources.h"
#include "pbl/util/crc32.h"

#define GFX_WIDTH PBL_DISPLAY_WIDTH
#define GFX_HEIGHT PBL_DISPLAY_HEIGHT
#define GFX_FONT_RESOURCE_ID 1u

#define PREVIEW_X 0
#define PREVIEW_Y 72
#define PREVIEW_WIDTH 200
#define PREVIEW_HEIGHT 80
#define PREVIEW_COLS 50
#define PREVIEW_ROWS 20

static FrameBuffer s_framebuffer;
static GContext s_context;
static FontInfo s_font;

void gfx_port_set_context(GContext *context);

static bool prv_render(void) {
  const GSize size = GSize(GFX_WIDTH, GFX_HEIGHT);
  framebuffer_init(&s_framebuffer, &size);
  graphics_context_init(&s_context, &s_framebuffer, GContextInitializationMode_System);
  gfx_port_set_context(&s_context);

  graphics_context_set_fill_color(&s_context, GColorOxfordBlue);
  graphics_fill_rect(&s_context, &GRect(0, 0, GFX_WIDTH, GFX_HEIGHT));

  graphics_context_set_fill_color(&s_context, GColorDarkCandyAppleRed);
  graphics_fill_rect(&s_context, &GRect(8, 8, GFX_WIDTH - 16, 10));

  graphics_context_set_fill_color(&s_context, GColorIslamicGreen);
  graphics_fill_rect(&s_context, &GRect(8, GFX_HEIGHT - 18, GFX_WIDTH - 16, 10));

  if (!text_resources_init_font(SYSTEM_APP, GFX_FONT_RESOURCE_ID, 0, &s_font)) {
    printk("GFX_FONT_ERROR\n");
    return false;
  }

  graphics_context_set_text_color(&s_context, GColorWhite);
  graphics_draw_text(&s_context, "12:34", &s_font,
                     GRect(0, PREVIEW_Y, GFX_WIDTH, PREVIEW_HEIGHT),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  return true;
}

static void prv_print_preview(void) {
  printk("GFX_PREVIEW %dx%d crop=%d,%d+%dx%d\n", PREVIEW_COLS, PREVIEW_ROWS,
         PREVIEW_X, PREVIEW_Y, PREVIEW_WIDTH, PREVIEW_HEIGHT);

  for (int row = 0; row < PREVIEW_ROWS; ++row) {
    const int y0 = PREVIEW_Y + row * PREVIEW_HEIGHT / PREVIEW_ROWS;
    const int y1 = PREVIEW_Y + (row + 1) * PREVIEW_HEIGHT / PREVIEW_ROWS;
    printk("|");
    for (int col = 0; col < PREVIEW_COLS; ++col) {
      const int x0 = PREVIEW_X + col * PREVIEW_WIDTH / PREVIEW_COLS;
      const int x1 = PREVIEW_X + (col + 1) * PREVIEW_WIDTH / PREVIEW_COLS;
      bool text_pixel = false;
      for (int y = y0; y < y1 && !text_pixel; ++y) {
        const uint8_t *line = framebuffer_get_line(&s_framebuffer, y);
        for (int x = x0; x < x1; ++x) {
          if (line[x] == GColorWhite.argb) {
            text_pixel = true;
            break;
          }
        }
      }
      printk("%c", text_pixel ? '#' : ' ');
    }
    printk("|\n");
  }
}

int main(void) {
  if (!prv_render()) {
    return 1;
  }

  const size_t framebuffer_size = framebuffer_get_size_bytes(&s_framebuffer);
  const uint32_t framebuffer_crc = crc32(CRC32_INIT, s_framebuffer.buffer, framebuffer_size);

  printk("GFX_CRC 0x%08x %dx%d 8bpp\n", framebuffer_crc, GFX_WIDTH, GFX_HEIGHT);
  prv_print_preview();
  printk("GFX_DONE\n");
  return 0;
}
