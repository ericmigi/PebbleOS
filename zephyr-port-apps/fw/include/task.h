/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "FreeRTOS.h"
#include "portmacro.h"

BaseType_t xTaskCreateRestricted(const TaskParameters_t *parameters, TaskHandle_t *handle);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
TaskHandle_t xTaskGetIdleTaskHandle(void);
const char *pcTaskGetTaskName(TaskHandle_t handle);
UBaseType_t uxTaskGetNumberOfTasks(void);
UBaseType_t uxTaskGetSystemState(TaskStatus_t *statuses, UBaseType_t count,
                                 uint32_t *total_runtime);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t handle);
void vTaskSuspend(TaskHandle_t handle);
void vTaskAllocateMPURegions(TaskHandle_t handle, const MemoryRegion_t *regions);
void vTaskPrioritySet(TaskHandle_t handle, UBaseType_t priority);
eTaskState eTaskGetState(TaskHandle_t handle);

#define taskSCHEDULER_RUNNING 1
static inline BaseType_t xTaskGetSchedulerState(void) { return taskSCHEDULER_RUNNING; }
