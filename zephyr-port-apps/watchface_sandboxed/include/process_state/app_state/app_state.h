/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>

#include "applib/graphics/text_render.h"
#include "applib/tick_timer_service_private.h"

typedef struct GBitmap GBitmap;
typedef struct GContext GContext;
typedef struct Heap Heap;
typedef struct Layer Layer;
typedef struct UnobstructedAreaState UnobstructedAreaState;

typedef struct TextRenderState {
  SpecialCodepointHandlerCb special_codepoint_handler_cb;
  void *special_codepoint_handler_context;
} TextRenderState;

TickTimerServiceState *app_state_get_tick_timer_service_state(void);
GContext *app_state_get_graphics_context(void);
TextRenderState *app_state_get_text_render_state(void);
bool app_state_get_text_perimeter_debugging_enabled(void);
void app_state_set_text_perimeter_debugging_enabled(bool enabled);
GBitmap *app_state_legacy2_get_2bit_framebuffer(void);
Heap *app_state_get_heap(void);
bool heap_is_allocated(Heap *heap, void *ptr);
Layer **app_state_get_layer_tree_stack(void);
UnobstructedAreaState *app_state_get_unobstructed_area_state(void);
bool process_manager_compiled_with_legacy2_sdk(void);

// The single per-(privileged)-app user-data slot (system_app.c). Used by the
// real system apps (settings/window.c stashes its SettingsData here).
void *app_state_get_user_data(void);
void app_state_set_user_data(void *data);


struct WindowStack;
struct WindowStack *app_state_get_window_stack(void);
