/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "FreeRTOS.h"

#define portIN_CRITICAL() false

enum {
  portCANONICAL_REG_INDEX_R0 = 0,
  portCANONICAL_REG_INDEX_R1,
  portCANONICAL_REG_INDEX_R2,
  portCANONICAL_REG_INDEX_R3,
  portCANONICAL_REG_INDEX_R4,
  portCANONICAL_REG_INDEX_R5,
  portCANONICAL_REG_INDEX_R6,
  portCANONICAL_REG_INDEX_R7,
  portCANONICAL_REG_INDEX_R8,
  portCANONICAL_REG_INDEX_R9,
  portCANONICAL_REG_INDEX_R10,
  portCANONICAL_REG_INDEX_R11,
  portCANONICAL_REG_INDEX_R12,
  portCANONICAL_REG_INDEX_SP,
  portCANONICAL_REG_INDEX_LR,
  portCANONICAL_REG_INDEX_PC,
  portCANONICAL_REG_INDEX_XPSR,
  portCANONICAL_REG_COUNT,
};

typedef struct {
  const char *pcName;
  TaskHandle_t taskHandle;
  uint32_t registers[portCANONICAL_REG_COUNT];
} xPORT_TASK_INFO;

typedef void (*TaskListWalkCallback)(const xPORT_TASK_INFO *task_info,
                                     void *context);

void vTaskListWalk(TaskListWalkCallback callback, void *context);
