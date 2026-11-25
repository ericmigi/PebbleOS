/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "epd_bench.h"

#if defined(CONFIG_SHELL) && defined(CONFIG_DISPLAY_UC8279_X4)
#include <errno.h>
#include <string.h>

#include <pbl/board.h>
#include <pbl/crc/crc.h>
#include <pbl/drivers/display/ssd1677.h>
#include <pbl/kernel/time.h>
#include <pbl/services/compositor/compositor.h>
#include <pbl/services/compositor/compositor_display.h>
#include <pbl/services/compositor/compositor_private.h>
#include <pbl/services/evented_timer.h>
#include <pbl/shell/shell.h>

#include <kernel/event_loop.h>
#include <kernel/remote_input.h>
#include <process_management/process_manager.h>

// KernelMain owns the framebuffer and benchmark state. The shell waits for the completed result.
static PBL_SEM_DEFINE(s_done, 0, 1);
static PBL_SEM_DEFINE(s_single_run, 1, 1);
static struct {
  bool background, hold;
  unsigned target, frames, submitted, drives, start_drives, cleanup_pass;
  uint32_t start_ms, elapsed_ms, draw_cycles, submit_cycles;
  bool active, in_frame, patch, restored, cancelled, blink, pair;
  int error;
} s_run;

bool epd_bench_active(void) {
  return s_run.active;
}

static void prv_toggle_patch(FrameBuffer *fb) {
  if (s_run.blink) {
    if ((s_run.frames & 1U) == 0) {
      for (unsigned y = 120; y < 168; y++) {
        uint8_t *row = (uint8_t *)framebuffer_get_line(fb, y);
        for (unsigned byte = 12; byte < 18; byte++) {
          row[byte] ^= 0xff;
          if (s_run.pair) {
            row[byte + 32U] ^= 0xff;
          }
        }
      }
    }
    return;
  }
  unsigned byte_x = 8U + s_run.frames % 8U;
  for (unsigned y = 96; y < 112; y++) {
    uint8_t *row = (uint8_t *)framebuffer_get_line(fb, y);
    row[byte_x] ^= 0xff;
    row[byte_x + 1] ^= 0xff;
  }
}

static void prv_kick(void *unused) {
  if (s_run.active && !compositor_display_update_in_progress()) {
    compositor_redraw();
  }
}

void epd_bench_prepare(FrameBuffer *fb) {
  if (!s_run.active) {
    return;
  }
  if (!s_run.frames) {
    s_run.start_ms = (uint32_t)pbl_uptime_ms();
    s_run.start_drives = pbl_display_uc8279_completed_drives(DISPLAY);
  }
  s_run.in_frame = true;
  if (s_run.frames < s_run.target) {
    uint32_t start = pbl_cycle_get_32();
    prv_toggle_patch(fb);
    s_run.draw_cycles += pbl_cycle_get_32() - start;
    s_run.patch = true;
  }
}

void epd_bench_submit_time(uint32_t cycles) {
  if (s_run.in_frame && s_run.patch) {
    s_run.submit_cycles += cycles;
    s_run.submitted++;
  }
}

static void prv_restore(void *unused) {
  compositor_redraw();
}

void epd_bench_finish(bool success) {
  if (!s_run.active) {
    return;
  }
  bool patch = s_run.patch;
  if (patch) {
    prv_toggle_patch(compositor_get_framebuffer());
    s_run.patch = false;
  }
  if (s_run.in_frame) {
    uint32_t count = pbl_display_uc8279_completed_drives(DISPLAY);
    if (!success) {
      s_run.error = -EIO;
    }
    if (patch && success) {
      s_run.frames++;
      s_run.drives = count - s_run.start_drives;
      s_run.elapsed_ms = (uint32_t)pbl_uptime_ms() - s_run.start_ms;
    } else if (!patch) {
      if (s_run.cleanup_pass == 0U) {
        s_run.start_drives = count;
      } else {
        // A repeat of the restored base should cause no further physical drive.
        s_run.restored = success && count == s_run.start_drives;
      }
      s_run.cleanup_pass++;
    }
    s_run.in_frame = false;
  }
  if (s_run.error || s_run.cancelled || s_run.cleanup_pass >= 2U) {
    s_run.active = false;
    if (s_run.cancelled || s_run.error) {
      launcher_task_add_callback(prv_restore, nullptr);
    }
    if (s_run.cancelled || s_run.background) {
      pbl_sem_give(&s_single_run);
    } else {
      pbl_sem_give(&s_done);
    }
  } else if (s_run.hold && s_run.frames < s_run.target) {
    if (evented_timer_register(650, false, prv_kick, nullptr))
      return;
    launcher_task_add_callback(prv_kick, nullptr);
  } else {
    launcher_task_add_callback(prv_kick, nullptr);
  }
}

// The run semaphore protects this request until KernelMain finishes the benchmark.
static struct {
  unsigned frames;
  bool blink, pair, background, hold;
} s_request;

static void prv_start(void *unused) {
  s_run = (typeof(s_run)){
    .target = s_request.frames,
    .active = true,
    .blink = s_request.blink,
    .pair = s_request.pair,
    .background = s_request.background,
    .hold = s_request.hold,
  };
  prv_kick(nullptr);
}

static void prv_cancel(void *unused) {
  if (s_run.in_frame) {
    s_run.cancelled = true;
    return;
  }
  // Completion can race the shell timeout while this callback is queued.
  pbl_sem_take(&s_done, PBL_NO_WAIT);
  s_run.active = false;
  compositor_redraw();
  pbl_sem_give(&s_single_run);
}

static int prv_bench(const struct pbl_shell *sh, size_t argc, char **argv) {
  bool result = argc == 2 && strcmp(argv[1], "result") == 0;
  bool background = argc == 3 && (strcmp(argv[2], "async") == 0 || strcmp(argv[2], "hold") == 0);
  unsigned long frames = 12;
  if ((argc == 3 && !background) ||
      (!result && argc > 1 && (pbl_shell_strtoul(argv[1], &frames) || frames < 2 || frames > 384)))
    return -EINVAL;
  if (pbl_sem_take(&s_single_run, PBL_NO_WAIT) != 0)
    return -EBUSY;
  int ret = 0;
  if (!result) {
    s_request.frames = (unsigned)frames;
    s_request.pair = strcmp(argv[0], "pair") == 0;
    if (s_request.pair && FRAMEBUFFER_BYTES_PER_ROW < 50U) {
      pbl_sem_give(&s_single_run);
      return -ENOTSUP;
    }
    s_request.blink = s_request.pair || strcmp(argv[0], "blink") == 0;
    s_request.background = background;
    s_request.hold = background && strcmp(argv[2], "hold") == 0;
    launcher_task_add_callback(prv_start, nullptr);
    if (background)
      return 0;
    ret = pbl_sem_take(&s_done, PBL_MSEC(120000));
    if (ret) {
      launcher_task_add_callback(prv_cancel, nullptr);
      return ret;
    }
  }
  uint32_t hz = pbl_cycles_per_sec();
  pbl_shell_print(sh, "%u submitted, %u completed, %u physical refreshes in %u ms", s_run.submitted,
                  s_run.frames, s_run.drives, (unsigned)s_run.elapsed_ms);
  pbl_shell_print(sh, "patch rendering %u us total, display submission %u us total",
                  (unsigned)(s_run.draw_cycles * 1000000ULL / hz),
                  (unsigned)(s_run.submit_cycles * 1000000ULL / hz));
  pbl_shell_print(sh, "restored frame repeat: %s",
                  s_run.restored ? "unchanged" : "changed or interrupted");
  ret = s_run.error;
  pbl_sem_give(&s_single_run);
  return ret;
}

// Exercise the foreground app's normal window and glyph drawing at a 50 ms cadence.
// Only one request can be outstanding. App completions and composition run on KernelMain.
static struct {
  EventedTimerID timer;
  uint32_t start_ms, elapsed_ms, request_cycle, max_latency, compose_cycles, start_drives, drives;
  uint32_t render_ticks, max_render_ticks;
  unsigned requests, frames, compositions, missed, total_renders;
  unsigned moves, distinct_frames;
  uint32_t previous_crc, first_ready_ms, last_ready_ms, last_change_ms;
  uint16_t change_ms[24];
  unsigned peak_drives, peak_ms;
  uint8_t last_sequence;
  bool pending, motion, changing_text, observe;
} s_app, s_observed;

uint32_t epd_appbench_compose_start(void) {
  return s_app.timer ? pbl_cycle_get_32() : 0;
}

void epd_appbench_compose_done(uint32_t start) {
  if (s_app.timer) {
    s_app.compose_cycles += pbl_cycle_get_32() - start;
    s_app.compositions++;
  }
}

void epd_appbench_ready(uint32_t render_ticks, uint8_t sequence) {
  if (!s_app.timer) {
    return;
  }
  s_app.render_ticks += render_ticks;
  s_app.max_render_ticks = PBL_MAX(s_app.max_render_ticks, render_ticks);
  if (s_app.observe) {
    // KernelMain owns the completed app framebuffer until the ready event releases it.
    GBitmap bitmap = compositor_get_app_framebuffer_as_bitmap();
    if (gbitmap_is_scaled2(&bitmap)) {
      bitmap = gbitmap_scaled2_get_stored(&bitmap);
    }
    size_t bytes = bitmap.row_size_bytes * (size_t)bitmap.bounds.size.h;
    uint32_t crc = pbl_crc32(0, bitmap.addr, bytes);
    uint32_t now = (uint32_t)pbl_uptime_ms() - s_app.start_ms;
    if (s_app.total_renders == 0) {
      s_app.first_ready_ms = now;
    } else if (crc != s_app.previous_crc) {
      if (s_app.distinct_frames < PBL_ARRAY_SIZE(s_app.change_ms)) {
        s_app.change_ms[s_app.distinct_frames] = now;
      }
      s_app.distinct_frames++;
      s_app.last_change_ms = now;
    }
    s_app.last_ready_ms = now;
    s_app.previous_crc = crc;
  }
  s_app.total_renders++;
  if (s_app.changing_text && sequence && sequence != s_app.last_sequence) {
    s_app.last_sequence = sequence;
    s_app.distinct_frames++;
  }
  if (s_app.pending) {
    uint32_t elapsed = pbl_cycle_get_32() - s_app.request_cycle;
    s_app.max_latency = PBL_MAX(s_app.max_latency, elapsed);
    s_app.frames++;
    s_app.pending = false;
  }
}

// Sample panel completions on KernelMain without logging in the display timing path.
static struct {
  uint32_t elapsed, drives;
} s_drive_samples[24];
static unsigned s_drive_sample_count;

static void prv_observe_cadence(void) {
  uint32_t count = pbl_display_uc8279_completed_drives(DISPLAY) - s_app.start_drives;
  unsigned available = PBL_MIN(s_drive_sample_count, PBL_ARRAY_SIZE(s_drive_samples));
  for (unsigned i = 0; i < available; i++) {
    unsigned slot = (s_drive_sample_count - 1U - i) % PBL_ARRAY_SIZE(s_drive_samples);
    unsigned elapsed = s_app.elapsed_ms - s_drive_samples[slot].elapsed;
    if (elapsed >= 1000U) {
      unsigned drives = count - s_drive_samples[slot].drives;
      if (!s_app.peak_ms || drives * s_app.peak_ms > s_app.peak_drives * elapsed) {
        s_app.peak_drives = drives;
        s_app.peak_ms = elapsed;
      }
      break;
    }
  }
  unsigned slot = s_drive_sample_count++ % PBL_ARRAY_SIZE(s_drive_samples);
  s_drive_samples[slot].elapsed = s_app.elapsed_ms;
  s_drive_samples[slot].drives = count;
}

static void prv_app_tick(void *unused) {
  s_app.elapsed_ms = (uint32_t)pbl_uptime_ms() - s_app.start_ms;
  // Four down/up pairs through the actual input and menu layers, ending on the initial row.
  if (s_app.motion && s_app.moves < 8 && s_app.elapsed_ms >= 300U + 500U * s_app.moves) {
    ButtonId button = s_app.moves % 2U ? BUTTON_ID_UP : BUTTON_ID_DOWN;
    if (remote_input_button_press(button, 1, 25, 0) == RemoteInputResult_Ok) {
      s_app.moves++;
    }
  }
  if (s_app.observe) {
    prv_observe_cadence();
    if (s_app.elapsed_ms < 4000U) {
      return;
    }
  }
  if (s_app.elapsed_ms >= (s_app.observe ? 4000U : 5000U) &&
      (s_app.moves % 2U == 0 || s_app.elapsed_ms >= 6000U)) {
    if (s_app.changing_text) {
      PebbleEvent restore = {.type = PEBBLE_RENDER_REQUEST_EVENT, .render.sequence = UINT8_MAX};
      process_manager_send_event_to_process(PebbleTask_App, &restore);
    }
    evented_timer_cancel(s_app.timer);
    s_app.timer = EVENTED_TIMER_INVALID_ID;
    s_app.drives = pbl_display_uc8279_completed_drives(DISPLAY) - s_app.start_drives;
    if (s_app.observe) {
      s_observed = s_app;
      pbl_sem_give(&s_single_run);
    } else {
      pbl_sem_give(&s_done);
    }
  } else if (s_app.pending) {
    s_app.missed++;
  } else {
    PebbleEvent request = {
      .type = PEBBLE_RENDER_REQUEST_EVENT,
      .render.sequence = s_app.changing_text ? s_app.requests + 1U : 0,
    };
    s_app.request_cycle = pbl_cycle_get_32();
    s_app.pending = process_manager_send_event_to_process(PebbleTask_App, &request);
    s_app.requests += s_app.pending;
  }
}

static void prv_start_appbench(void *mode) {
  s_drive_sample_count = 0;
  s_app = (typeof(s_app)){
    .motion = (uintptr_t)mode == 1,
    .observe = (uintptr_t)mode == 3,
    .changing_text = (uintptr_t)mode == 2,
    .start_ms = (uint32_t)pbl_uptime_ms(),
    .start_drives = pbl_display_uc8279_completed_drives(DISPLAY),
    .timer = evented_timer_register(50, true, prv_app_tick, nullptr),
  };
  if (s_app.timer) {
    prv_app_tick(nullptr);
  } else if (s_app.observe) {
    s_observed = s_app;
    pbl_sem_give(&s_single_run);
  } else {
    pbl_sem_give(&s_done);
  }
}

static int prv_observe_result(const struct pbl_shell *sh) {
  if (!s_observed.elapsed_ms) {
    pbl_shell_print(sh, "No completed observation");
    return -EAGAIN;
  }

  pbl_shell_print(sh,
                  "%u ms observed, %u app draws, %u changed app frames after first, "
                  "%u panel drives",
                  (unsigned)s_observed.elapsed_ms, s_observed.total_renders,
                  s_observed.distinct_frames, (unsigned)s_observed.drives);
  pbl_shell_print(sh,
                  "first and last ready %u/%u ms, last change %u ms, "
                  "app render total/max %u/%u ms",
                  (unsigned)s_observed.first_ready_ms, (unsigned)s_observed.last_ready_ms,
                  (unsigned)s_observed.last_change_ms,
                  (unsigned)(1000ULL * s_observed.render_ticks / PBL_TICK_HZ),
                  (unsigned)(1000ULL * s_observed.max_render_ticks / PBL_TICK_HZ));
  pbl_shell_print(sh, "fastest sampled second: %u panel drives in %u ms", s_observed.peak_drives,
                  s_observed.peak_ms);
  unsigned samples = PBL_MIN(s_observed.distinct_frames, PBL_ARRAY_SIZE(s_observed.change_ms));
  for (unsigned i = 0; i < samples; i++) {
    pbl_shell_print(sh, "changed frame %u at %u ms", i + 1, s_observed.change_ms[i]);
  }
  return 0;
}

static int prv_appbench(const struct pbl_shell *sh, size_t argc, char **argv) {
  bool motion = argc == 2 && strcmp(argv[1], "menu") == 0;
  bool text = argc == 2 && strcmp(argv[1], "text") == 0;
  bool observe = argc == 2 && strcmp(argv[1], "observe") == 0;
  bool result = argc == 2 && strcmp(argv[1], "result") == 0;
  if (argc == 2 && !motion && !text && !observe && !result) {
    return -EINVAL;
  }
  if (pbl_sem_take(&s_single_run, PBL_NO_WAIT) != 0) {
    return -EBUSY;
  }
  if (result) {
    int rc = prv_observe_result(sh);
    pbl_sem_give(&s_single_run);
    return rc;
  }
  launcher_task_add_callback(prv_start_appbench, (void *)(uintptr_t)(observe ? 3
                                                                     : text  ? 2
                                                                             : motion));
  if (observe) {
    pbl_shell_print(sh, "Observing real app frames for 4 seconds; use epd appbench result");
    return 0;
  }
  pbl_sem_take(&s_done, PBL_FOREVER);
  uint32_t hz = pbl_cycles_per_sec();
  pbl_shell_print(sh, "%u native redraw requests, %u app frames ready in %u ms; %u missed ticks",
                  s_app.requests, s_app.frames, (unsigned)s_app.elapsed_ms, s_app.missed);
  pbl_shell_print(sh, "longest request to ready %u us; %u compositions in %u us; %u panel drives",
                  (unsigned)(s_app.max_latency * 1000000ULL / hz), s_app.compositions,
                  (unsigned)(s_app.compose_cycles * 1000000ULL / hz), (unsigned)s_app.drives);
  if (text) {
    pbl_shell_print(sh, "%u distinct native frame labels drawn", s_app.distinct_frames);
  }
  if (motion) {
    pbl_shell_print(sh, "%u menu down/up pairs injected%s", s_app.moves / 2U,
                    s_app.moves % 2U ? ", last down unpaired" : "");
  }
  if (s_app.total_renders) {
    pbl_shell_print(sh, "%u native window draws, avg %u us, max %u us (including preemption)",
                    s_app.total_renders,
                    (unsigned)(s_app.render_ticks * 1000000ULL / PBL_TICK_HZ / s_app.total_renders),
                    (unsigned)(s_app.max_render_ticks * 1000000ULL / PBL_TICK_HZ));
  }
  int ret = s_app.frames ? 0 : -EAGAIN;
  pbl_sem_give(&s_single_run);
  return ret;
}

PBL_SHELL_SUBCMD_ADD(sub_epd, appbench, nullptr,
                     "Measure redraws [menu|text] or real app frames [observe|result]",
                     prv_appbench, 1, 1);
PBL_SHELL_SUBCMD_ADD(sub_epd, pair, nullptr,
                     "Alternate two separated patches [frames [async|hold] | result]", prv_bench, 1, 2);
PBL_SHELL_SUBCMD_ADD(sub_epd, blink, nullptr,
                     "Alternate a 48x48 patch [frames [async|hold] | result]", prv_bench, 1, 2);
PBL_SHELL_SUBCMD_ADD(sub_epd, bench, nullptr,
                     "Measure moving-patch frames [frames [async|hold] | result]", prv_bench, 1, 2);
#endif
