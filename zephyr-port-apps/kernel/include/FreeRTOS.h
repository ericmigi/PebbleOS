/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdint.h>

#include <zephyr/kernel.h>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef void *QueueHandle_t;
typedef void *SemaphoreHandle_t;
typedef void *TaskHandle_t;
typedef uint32_t TickType_t;

#define pdFALSE 0
#define pdTRUE 1
#define pdFAIL 0
#define pdPASS 1

#define configTICK_RATE_HZ CONFIG_SYS_CLOCK_TICKS_PER_SEC
#define portMAX_DELAY UINT32_MAX

void pebble_zephyr_semaphore_give(SemaphoreHandle_t semaphore);

static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore) {
  pebble_zephyr_semaphore_give(semaphore);
  return pdTRUE;
}
