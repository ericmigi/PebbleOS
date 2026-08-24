/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "pbl/os/assert.h"
#include "pbl/os/mutex.h"
#include "pbl/os/semaphore.h"
#include "pbl/os/tick.h"
#include "pbl/util/assert.h"
#include "pbl/util/circular_buffer.h"
#include "pbl/util/crc32.h"

#define CONTENTION_STACK_SIZE 1024
#define CONTENTION_PRIORITY 5

typedef const char *(*SmokeTest)(void);

static PebbleMutex *s_contention_mutex;
static struct k_sem s_contention_locked;
static struct k_sem s_contention_attempted;
static struct k_sem s_contention_release;
static struct k_sem s_contention_done;
static bool s_contention_timed_out;
static bool s_contention_acquired;
static PebbleSemaphore *s_handoff_semaphore;
static struct k_sem s_handoff_ready;
static struct k_sem s_handoff_release;
static struct k_sem s_handoff_done;

static struct k_thread s_contention_thread_a;
static struct k_thread s_contention_thread_b;
static struct k_thread s_handoff_thread;
K_THREAD_STACK_DEFINE(s_contention_stack_a, CONTENTION_STACK_SIZE);
K_THREAD_STACK_DEFINE(s_contention_stack_b, CONTENTION_STACK_SIZE);
K_THREAD_STACK_DEFINE(s_handoff_stack, CONTENTION_STACK_SIZE);

NORETURN util_assertion_failed(const char *filename, int line) {
  os_assertion_failed(filename, line);
}

static void prv_contention_owner(void *arg1, void *arg2, void *arg3) {
  ARG_UNUSED(arg1);
  ARG_UNUSED(arg2);
  ARG_UNUSED(arg3);

  mutex_lock(s_contention_mutex);
  k_sem_give(&s_contention_locked);
  k_sem_take(&s_contention_release, K_FOREVER);
  mutex_unlock(s_contention_mutex);
  k_sem_give(&s_contention_done);
}

static void prv_contention_waiter(void *arg1, void *arg2, void *arg3) {
  ARG_UNUSED(arg1);
  ARG_UNUSED(arg2);
  ARG_UNUSED(arg3);

  k_sem_take(&s_contention_locked, K_FOREVER);
  s_contention_timed_out = !mutex_lock_with_timeout(s_contention_mutex, 20);
  k_sem_give(&s_contention_attempted);

  s_contention_acquired = mutex_lock_with_timeout(s_contention_mutex, 1000);
  if (s_contention_acquired) {
    mutex_unlock(s_contention_mutex);
  }
  k_sem_give(&s_contention_done);
}

static void prv_semaphore_giver(void *arg1, void *arg2, void *arg3) {
  ARG_UNUSED(arg1);
  ARG_UNUSED(arg2);
  ARG_UNUSED(arg3);

  k_sem_give(&s_handoff_ready);
  k_sem_take(&s_handoff_release, K_FOREVER);
  semaphore_give(s_handoff_semaphore);
  k_sem_give(&s_handoff_done);
}

static const char *prv_test_mutex_lock_unlock(void) {
  PebbleMutex *mutex = mutex_create();
  if (mutex == NULL) {
    return "create";
  }

  mutex_assert_held_by_curr_task(mutex, false);
  mutex_lock(mutex);
  mutex_assert_held_by_curr_task(mutex, true);
  if (mutex_lock_with_timeout(mutex, 0)) {
    return "plain-recursed";
  }
  mutex_unlock(mutex);

  if (!mutex_lock_with_timeout(mutex, 100)) {
    return "timed-lock";
  }
  mutex_unlock(mutex);
  mutex_lock_with_lr(mutex, 0x1234);
  mutex_unlock(mutex);
  mutex_destroy(mutex);
  return NULL;
}

static const char *prv_test_mutex_recursive(void) {
  PebbleRecursiveMutex *mutex = mutex_create_recursive();
  if (mutex == NULL) {
    return "create";
  }

  mutex_assert_recursive_held_by_curr_task(mutex, false);
  mutex_lock_recursive(mutex);
  if (!mutex_is_owned_recursive(mutex)) {
    return "not-owned";
  }
  if (!mutex_lock_recursive_with_timeout(mutex, 0)) {
    return "relock";
  }
  if (!mutex_lock_recursive_with_timeout_and_lr(mutex, 0, 0x5678)) {
    return "relock-lr";
  }

  mutex_unlock_recursive(mutex);
  mutex_unlock_recursive(mutex);
  if (!mutex_is_owned_recursive(mutex)) {
    return "released-early";
  }
  mutex_unlock_recursive(mutex);
  if (mutex_is_owned_recursive(mutex)) {
    return "still-owned";
  }
  mutex_destroy_recursive(mutex);
  return NULL;
}

static const char *prv_test_semaphore(void) {
  PebbleSemaphore *semaphore = semaphore_create();
  if (semaphore == NULL) {
    return "create";
  }
  if (semaphore_take_with_timeout(semaphore, 0)) {
    return "not-empty";
  }
  semaphore_give(semaphore);
  if (!semaphore_take_with_timeout(semaphore, 0)) {
    return "take";
  }
  semaphore_destroy(semaphore);
  return NULL;
}

static const char *prv_test_semaphore_handoff(void) {
  s_handoff_semaphore = semaphore_create();
  if (s_handoff_semaphore == NULL) {
    return "create";
  }

  k_sem_init(&s_handoff_ready, 0, 1);
  k_sem_init(&s_handoff_release, 0, 1);
  k_sem_init(&s_handoff_done, 0, 1);
  k_thread_create(&s_handoff_thread, s_handoff_stack, K_THREAD_STACK_SIZEOF(s_handoff_stack),
                  prv_semaphore_giver, NULL, NULL, NULL, CONTENTION_PRIORITY, 0, K_NO_WAIT);

  if (k_sem_take(&s_handoff_ready, K_SECONDS(1)) != 0) {
    return "thread-start";
  }
  k_sem_give(&s_handoff_release);
  if (!semaphore_take_with_timeout(s_handoff_semaphore, 1000)) {
    return "take-timeout";
  }
  if (k_sem_take(&s_handoff_done, K_SECONDS(1)) != 0) {
    return "thread-timeout";
  }

  semaphore_destroy(s_handoff_semaphore);
  return NULL;
}

static const char *prv_test_mutex_contention(void) {
  s_contention_mutex = mutex_create();
  s_contention_timed_out = false;
  s_contention_acquired = false;
  k_sem_init(&s_contention_locked, 0, 1);
  k_sem_init(&s_contention_attempted, 0, 1);
  k_sem_init(&s_contention_release, 0, 1);
  k_sem_init(&s_contention_done, 0, 2);

  k_thread_create(&s_contention_thread_a, s_contention_stack_a,
                  K_THREAD_STACK_SIZEOF(s_contention_stack_a), prv_contention_owner, NULL, NULL,
                  NULL, CONTENTION_PRIORITY, 0, K_NO_WAIT);
  k_thread_create(&s_contention_thread_b, s_contention_stack_b,
                  K_THREAD_STACK_SIZEOF(s_contention_stack_b), prv_contention_waiter, NULL, NULL,
                  NULL, CONTENTION_PRIORITY, 0, K_NO_WAIT);

  if (k_sem_take(&s_contention_attempted, K_SECONDS(1)) != 0) {
    return "attempt-timeout";
  }
  k_sem_give(&s_contention_release);
  if (k_sem_take(&s_contention_done, K_SECONDS(1)) != 0 ||
      k_sem_take(&s_contention_done, K_SECONDS(1)) != 0) {
    return "thread-timeout";
  }

  mutex_destroy(s_contention_mutex);
  if (!s_contention_timed_out) {
    return "no-contention";
  }
  if (!s_contention_acquired) {
    return "no-handoff";
  }
  return NULL;
}

static const char *prv_test_tick_conversions(void) {
  TickType_t one_second = milliseconds_to_ticks(1000);
  if (one_second != CONFIG_SYS_CLOCK_TICKS_PER_SEC) {
    return "ms-to-ticks";
  }
  if (ticks_to_milliseconds(one_second) != 1000) {
    return "ticks-to-ms";
  }

  const uint32_t sample_ms = 1234;
  TickType_t expected_ticks =
      ((uint64_t)sample_ms * CONFIG_SYS_CLOCK_TICKS_PER_SEC) / 1000;
  if (milliseconds_to_ticks(sample_ms) != expected_ticks) {
    return "rounding";
  }
  return NULL;
}

static const char *prv_test_circular_buffer(void) {
  CircularBuffer buffer;
  uint8_t storage[8];
  uint8_t output[8] = {0};

  circular_buffer_init_ex(&buffer, storage, sizeof(storage), false);
  if (!circular_buffer_write(&buffer, "abcd", 4) || !circular_buffer_consume(&buffer, 3)) {
    return "prime";
  }
  if (!circular_buffer_write(&buffer, "EFGHIJ", 6)) {
    return "wrapped-write";
  }
  if (circular_buffer_copy(&buffer, output, sizeof(output)) != 7) {
    return "copy-length";
  }
  if (memcmp(output, "dEFGHIJ", 7) != 0) {
    return "roundtrip";
  }
  return NULL;
}

static const char *prv_test_crc32(void) {
  uint32_t crc = crc32(CRC32_INIT, "123456789", 9);
  if (crc != 0xcbf43926) {
    return "standard-vector";
  }
  return NULL;
}

static bool prv_run_test(const char *name, SmokeTest test) {
  const char *detail = test();
  if (detail == NULL) {
    printk("SMOKE_PASS %s\n", name);
    return true;
  }

  printk("SMOKE_FAIL %s %s\n", name, detail);
  return false;
}

int main(void) {
  unsigned int passed = 0;
  unsigned int total = 0;

#define RUN_TEST(name, function)                                                                   \
  do {                                                                                             \
    total++;                                                                                       \
    if (prv_run_test(name, function)) {                                                            \
      passed++;                                                                                    \
    }                                                                                              \
  } while (0)

  RUN_TEST("mutex_lock_unlock", prv_test_mutex_lock_unlock);
  RUN_TEST("mutex_recursive", prv_test_mutex_recursive);
  RUN_TEST("mutex_contention", prv_test_mutex_contention);
  RUN_TEST("semaphore", prv_test_semaphore);
  RUN_TEST("semaphore_handoff", prv_test_semaphore_handoff);
  RUN_TEST("tick_conversions", prv_test_tick_conversions);
  RUN_TEST("circular_buffer", prv_test_circular_buffer);
  RUN_TEST("crc32", prv_test_crc32);

  printk("SMOKE_DONE %u/%u\n", passed, total);
  return 0;
}
