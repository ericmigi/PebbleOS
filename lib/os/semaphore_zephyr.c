/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>

#include "pbl/os/malloc.h"
#include "pbl/os/semaphore.h"
#include "pbl/os/tick.h"

struct pebble_semaphore_t {
  struct k_sem semaphore;
};

static k_timeout_t prv_timeout_from_ms(uint32_t timeout_ms) {
  TickType_t ticks = milliseconds_to_ticks(timeout_ms);
  return ticks == 0 ? K_NO_WAIT : K_TICKS(ticks - 1);
}

PebbleSemaphore *semaphore_create(void) {
  PebbleSemaphore *semaphore = os_malloc(sizeof(*semaphore));
  if (semaphore == NULL) {
    return NULL;
  }

  if (k_sem_init(&semaphore->semaphore, 0, 1) != 0) {
    os_free(semaphore);
    return NULL;
  }
  return semaphore;
}

void semaphore_destroy(PebbleSemaphore *handle) {
  os_free(handle);
}

void semaphore_take(PebbleSemaphore *handle) {
  (void)k_sem_take(&handle->semaphore, K_FOREVER);
}

bool semaphore_take_with_timeout(PebbleSemaphore *handle, uint32_t timeout_ms) {
  return k_sem_take(&handle->semaphore, prv_timeout_from_ms(timeout_ms)) == 0;
}

void semaphore_give(PebbleSemaphore *handle) {
  k_sem_give(&handle->semaphore);
}
