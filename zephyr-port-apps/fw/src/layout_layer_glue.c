/* SPDX-License-Identifier: Apache-2.0 */

// The small LayoutLayer accessors the timeline layout engine calls
// (layout_get_colors etc), lifted from src/fw/services/timeline/layout_layer.c.
// The full file can't be compiled in: its s_constructors[] dispatch table
// references every timeline layout family constructor (calendar/weather/health/
// sports/alarm/generic), which would drag those whole closures in. The port
// creates the notification layout directly (notification_layout_create), so only
// these mode-agnostic accessors are needed.

#include "applib/graphics/gcolor_definitions.h"
#include "applib/graphics/gtypes.h"
#include "pbl/services/timeline/layout_layer.h"

static const LayoutColors s_default_colors = {
  .primary_color = { .argb = GColorBlackARGB8 },
  .secondary_color = { .argb = GColorBlackARGB8 },
  .bg_color = { .argb = PBL_IF_COLOR_ELSE(GColorLightGrayARGB8, GColorWhiteARGB8) },
};

GSize layout_get_size(GContext *ctx, LayoutLayer *layout) {
  return layout->impl->size_getter(ctx, layout);
}

const LayoutColors *layout_get_colors(const LayoutLayer *layout) {
#if PBL_COLOR
  if (layout->impl->color_getter) {
    return layout->impl->color_getter(layout);
  }
#endif
  return &s_default_colors;
}

const LayoutColors *layout_get_notification_colors(const LayoutLayer *layout) {
  return layout_get_colors(layout);
}

void layout_set_mode(LayoutLayer *layout, LayoutLayerMode final_mode) {
  layout->impl->mode_setter(layout, final_mode);
}

void *layout_get_context(LayoutLayer *layout) {
  if (layout->impl->context_getter) {
    return layout->impl->context_getter(layout);
  }
  return NULL;
}

void layout_destroy(LayoutLayer *layout) {
  layout->impl->destructor(layout);
}
