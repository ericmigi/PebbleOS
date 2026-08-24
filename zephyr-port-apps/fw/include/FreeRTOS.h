/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdint.h>

#include <zephyr/kernel.h>

typedef int BaseType_t;
#define portBASE_TYPE int
typedef unsigned int UBaseType_t;
typedef void *QueueHandle_t;
typedef void *QueueSetHandle_t;
typedef void *QueueSetMemberHandle_t;
typedef void *SemaphoreHandle_t;
typedef void *TaskHandle_t;
typedef uint32_t TickType_t;
typedef uint32_t StackType_t;
typedef StackType_t portSTACK_TYPE;

#define pdFALSE 0
#define pdTRUE 1
#define pdFAIL 0
#define pdPASS 1

#define configMAX_PRIORITIES 8
#define configTICK_RATE_HZ CONFIG_SYS_CLOCK_TICKS_PER_SEC
#define portMAX_DELAY UINT32_MAX
#define portPRIVILEGE_BIT 0
#define tskIDLE_PRIORITY 0

typedef struct {
  TaskHandle_t xHandle;
  uint32_t ulRunTimeCounter;
} TaskStatus_t;

typedef enum {
  eRunning,
  eReady,
  eBlocked,
  eSuspended,
  eDeleted,
  eInvalid,
} eTaskState;

typedef struct xMEMORY_REGION {
  void *pvBaseAddress;
  uint32_t ulLengthInBytes;
  uint32_t ulParameters;
} MemoryRegion_t;

#define portNUM_CONFIGURABLE_REGIONS 4

typedef void (*TaskFunction_t)(void *);

typedef struct xTASK_PARAMETERS {
  TaskFunction_t pvTaskCode;
  const char *pcName;
  uint32_t usStackDepth;
  UBaseType_t uxPriority;
  StackType_t *puxStackBuffer;
  MemoryRegion_t xRegions[portNUM_CONFIGURABLE_REGIONS];
  void *pvParameters;
} TaskParameters_t;
