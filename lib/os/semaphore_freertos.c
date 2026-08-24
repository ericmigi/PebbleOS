/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "pbl/os/semaphore.h"

#include "FreeRTOS.h"
#include "semphr.h"

#include "pbl/os/tick.h"

PebbleSemaphore *semaphore_create(void) {
  return (PebbleSemaphore *)xSemaphoreCreateBinary();
}

void semaphore_destroy(PebbleSemaphore *handle) {
  vSemaphoreDelete((SemaphoreHandle_t)handle);
}

void semaphore_take(PebbleSemaphore *handle) {
  (void)xSemaphoreTake((SemaphoreHandle_t)handle, portMAX_DELAY);
}

bool semaphore_take_with_timeout(PebbleSemaphore *handle, uint32_t timeout_ms) {
  return xSemaphoreTake((SemaphoreHandle_t)handle, milliseconds_to_ticks(timeout_ms)) == pdTRUE;
}

void semaphore_give(PebbleSemaphore *handle) {
  (void)xSemaphoreGive((SemaphoreHandle_t)handle);
}
