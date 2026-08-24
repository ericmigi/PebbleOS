/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "FreeRTOS.h"

SemaphoreHandle_t fw_semaphore_create(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore);
BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t semaphore,
                                 BaseType_t *should_context_switch);

#define vSemaphoreCreateBinary(handle) ((handle) = fw_semaphore_create())
