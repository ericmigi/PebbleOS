/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <applib/graphics/framebuffer.h>
#if defined(CONFIG_SHELL) && defined(CONFIG_DISPLAY_UC8279_X4)
uint32_t epd_appbench_compose_start(void);
void epd_appbench_compose_done(uint32_t start);
void epd_appbench_ready(uint32_t render_ticks, uint8_t sequence);
bool epd_bench_active(void);
void epd_bench_prepare(FrameBuffer *fb);
void epd_bench_finish(bool success);
void epd_bench_submit_time(uint32_t cycles);
#else
static inline uint32_t epd_appbench_compose_start(void) {
  return 0;
}
static inline void epd_appbench_compose_done(uint32_t start) {
}
static inline void epd_appbench_ready(uint32_t render_ticks, uint8_t sequence) {
}
static inline bool epd_bench_active(void) {
  return false;
}
static inline void epd_bench_prepare(FrameBuffer *fb) {
}
static inline void epd_bench_finish(bool success) {
}
static inline void epd_bench_submit_time(uint32_t cycles) {
}
#endif
