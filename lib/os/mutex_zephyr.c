/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>

#include "pbl/os/assert.h"
#include "pbl/os/malloc.h"
#include "pbl/os/mutex.h"
#include "pbl/os/tick.h"

typedef struct {
  struct k_mutex zephyr_mutex;
  uint32_t lr;
} PebbleMutexCommon;

struct pebble_mutex_t {
  PebbleMutexCommon common;
};

struct pebble_recursive_mutex_t {
  PebbleMutexCommon common;
};

static PebbleMutexCommon *prv_create_mutex(void) {
  PebbleMutexCommon *mutex = os_malloc_check(sizeof(PebbleMutex));
  int rv = k_mutex_init(&mutex->zephyr_mutex);
  OS_ASSERT(rv == 0);
  mutex->lr = 0;
  return mutex;
}

static bool prv_lock(PebbleMutexCommon *mutex, k_timeout_t timeout, uint32_t lr) {
  int rv = k_mutex_lock(&mutex->zephyr_mutex, timeout);
  if (rv != 0) {
    return false;
  }

  if (mutex->lr == 0) {
    mutex->lr = lr;
  }
  return true;
}

static bool prv_lock_nonrecursive(PebbleMutexCommon *mutex, k_timeout_t timeout, uint32_t lr) {
  // Zephyr mutexes recurse natively; ordinary Pebble mutexes must not.
  if (mutex->zephyr_mutex.owner == k_current_get()) {
    k_sleep(timeout);
    return false;
  }
  return prv_lock(mutex, timeout, lr);
}

static k_timeout_t prv_timeout_from_ms(uint32_t timeout_ms) {
  TickType_t ticks = milliseconds_to_ticks(timeout_ms);
  // Zephyr adds one tick to relative timeouts.
  return ticks == 0 ? K_NO_WAIT : K_TICKS(ticks - 1);
}

PebbleMutex *mutex_create(void) {
  return (PebbleMutex *)prv_create_mutex();
}

void mutex_destroy(PebbleMutex *handle) {
  OS_ASSERT(handle != NULL);
  os_free(handle);
}

void mutex_lock(PebbleMutex *handle) {
  uintptr_t lr = (uintptr_t)__builtin_return_address(0);
  OS_ASSERT(!k_is_in_isr());
  OS_ASSERT(prv_lock_nonrecursive(&handle->common, K_FOREVER, lr));
}

bool mutex_lock_with_timeout(PebbleMutex *handle, uint32_t timeout_ms) {
  uintptr_t lr = (uintptr_t)__builtin_return_address(0);
  OS_ASSERT(!k_is_in_isr());
  return prv_lock_nonrecursive(&handle->common, prv_timeout_from_ms(timeout_ms), lr);
}

void mutex_lock_with_lr(PebbleMutex *handle, uint32_t lr) {
  OS_ASSERT(!k_is_in_isr());
  OS_ASSERT(prv_lock_nonrecursive(&handle->common, K_FOREVER, lr));
}

void mutex_unlock(PebbleMutex *handle) {
  OS_ASSERT(!k_is_in_isr());
  handle->common.lr = 0;
  OS_ASSERT(k_mutex_unlock(&handle->common.zephyr_mutex) == 0);
}

PebbleRecursiveMutex *mutex_create_recursive(void) {
  return (PebbleRecursiveMutex *)prv_create_mutex();
}

void mutex_destroy_recursive(PebbleRecursiveMutex *handle) {
  mutex_destroy((PebbleMutex *)handle);
}

void mutex_lock_recursive(PebbleRecursiveMutex *handle) {
  uintptr_t lr = (uintptr_t)__builtin_return_address(0);
  OS_ASSERT(!k_is_in_isr());
  OS_ASSERT(prv_lock(&handle->common, K_FOREVER, lr));
}

void mutex_lock_recursive_with_lr(PebbleRecursiveMutex *handle, uint32_t lr) {
  OS_ASSERT(!k_is_in_isr());
  OS_ASSERT(prv_lock(&handle->common, K_FOREVER, lr));
}

bool mutex_lock_recursive_with_timeout(PebbleRecursiveMutex *handle, uint32_t timeout_ms) {
  uintptr_t lr = (uintptr_t)__builtin_return_address(0);
  OS_ASSERT(!k_is_in_isr());
  return prv_lock(&handle->common, prv_timeout_from_ms(timeout_ms), lr);
}

bool mutex_lock_recursive_with_timeout_and_lr(PebbleRecursiveMutex *handle,
                                              uint32_t timeout_ms,
                                              uint32_t lr) {
  OS_ASSERT(!k_is_in_isr());
  return prv_lock(&handle->common, prv_timeout_from_ms(timeout_ms), lr);
}

bool mutex_is_owned_recursive(PebbleRecursiveMutex *handle) {
  return handle->common.zephyr_mutex.owner == k_current_get();
}

void mutex_unlock_recursive(PebbleRecursiveMutex *handle) {
  OS_ASSERT(!k_is_in_isr());
  if (handle->common.zephyr_mutex.lock_count == 1) {
    handle->common.lr = 0;
  }
  OS_ASSERT(k_mutex_unlock(&handle->common.zephyr_mutex) == 0);
}

static void prv_assert_held_by_current(PebbleMutexCommon *mutex, bool is_held, uint32_t lr) {
  OS_ASSERT_LR((mutex->zephyr_mutex.owner == k_current_get()) == is_held, lr);
}

void mutex_assert_held_by_curr_task(PebbleMutex *handle, bool is_held) {
  uintptr_t lr = (uintptr_t)__builtin_return_address(0);
  prv_assert_held_by_current(&handle->common, is_held, lr);
}

void mutex_assert_recursive_held_by_curr_task(PebbleRecursiveMutex *handle, bool is_held) {
  uintptr_t lr = (uintptr_t)__builtin_return_address(0);
  prv_assert_held_by_current(&handle->common, is_held, lr);
}
