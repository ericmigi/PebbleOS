/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>

#include <zephyr/kernel.h>

#include "FreeRTOS.h"
#include "pbl/os/semaphore.h"
#include "pbl/os/tick.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

typedef enum {
  FwQueueType_Queue,
  FwQueueType_Set,
} FwQueueType;

typedef struct FwQueueSet FwQueueSet;

typedef struct {
  FwQueueType type;
  struct k_msgq msgq;
  char *buffer;
  UBaseType_t length;
  FwQueueSet *set;
} FwQueue;

#define FW_QUEUE_SET_MEMBERS 4

struct FwQueueSet {
  FwQueueType type;
  struct k_sem ready;
  FwQueue *members[FW_QUEUE_SET_MEMBERS];
  size_t member_count;
};

static k_timeout_t prv_timeout(TickType_t ticks) {
  if (ticks == portMAX_DELAY) {
    return K_FOREVER;
  }
  return ticks == 0 ? K_NO_WAIT : K_TICKS(ticks);
}

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size) {
  FwQueue *queue = k_calloc(1, sizeof(*queue));
  if (!queue) {
    return NULL;
  }
  queue->buffer = k_malloc(length * item_size);
  if (!queue->buffer) {
    k_free(queue);
    return NULL;
  }
  queue->type = FwQueueType_Queue;
  queue->length = length;
  k_msgq_init(&queue->msgq, queue->buffer, item_size, length);
  return queue;
}

QueueSetHandle_t xQueueCreateSet(UBaseType_t length) {
  ARG_UNUSED(length);
  FwQueueSet *set = k_calloc(1, sizeof(*set));
  if (!set) {
    return NULL;
  }
  set->type = FwQueueType_Set;
  k_sem_init(&set->ready, 0, UINT_MAX);
  return set;
}

BaseType_t xQueueAddToSet(QueueHandle_t queue_handle, QueueSetHandle_t set_handle) {
  FwQueue *queue = queue_handle;
  FwQueueSet *set = set_handle;
  if (!queue || !set || queue->type != FwQueueType_Queue || set->type != FwQueueType_Set ||
      set->member_count == FW_QUEUE_SET_MEMBERS) {
    return pdFAIL;
  }
  set->members[set->member_count++] = queue;
  queue->set = set;
  return pdPASS;
}

static FwQueue *prv_ready_member(FwQueueSet *set) {
  for (size_t i = 0; i < set->member_count; ++i) {
    if (k_msgq_num_used_get(&set->members[i]->msgq) != 0) {
      return set->members[i];
    }
  }
  return NULL;
}

QueueSetMemberHandle_t xQueueSelectFromSet(QueueSetHandle_t set_handle, TickType_t ticks) {
  FwQueueSet *set = set_handle;
  if (!set || set->type != FwQueueType_Set) {
    return NULL;
  }

  FwQueue *ready = prv_ready_member(set);
  if (ready || ticks == 0) {
    return ready;
  }

  int result = k_sem_take(&set->ready, prv_timeout(ticks));
  if (result != 0) {
    return NULL;
  }
  return prv_ready_member(set);
}

BaseType_t xQueueSendToBack(QueueHandle_t queue_handle, const void *item, TickType_t ticks) {
  FwQueue *queue = queue_handle;
  if (!queue || queue->type != FwQueueType_Queue ||
      k_msgq_put(&queue->msgq, item, prv_timeout(ticks)) != 0) {
    return pdFAIL;
  }
  if (queue->set) {
    k_sem_give(&queue->set->ready);
  }
  return pdPASS;
}

BaseType_t xQueueSendToBackFromISR(QueueHandle_t queue, const void *item,
                                   BaseType_t *should_context_switch) {
  if (should_context_switch) {
    *should_context_switch = pdFALSE;
  }
  return xQueueSendToBack(queue, item, 0);
}

BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks) {
  return xQueueSendToBack(queue, item, ticks);
}

BaseType_t xQueueSendFromISR(QueueHandle_t queue, const void *item,
                             BaseType_t *should_context_switch) {
  return xQueueSendToBackFromISR(queue, item, should_context_switch);
}

BaseType_t xQueueReceive(QueueHandle_t queue_handle, void *item, TickType_t ticks) {
  FwQueue *queue = queue_handle;
  if (!queue || queue->type != FwQueueType_Queue) {
    return pdFAIL;
  }
  return k_msgq_get(&queue->msgq, item, prv_timeout(ticks)) == 0 ? pdPASS : pdFAIL;
}

BaseType_t xQueueReset(QueueHandle_t handle) {
  if (!handle) {
    return pdFAIL;
  }
  FwQueueType type = *(FwQueueType *)handle;
  if (type == FwQueueType_Queue) {
    k_msgq_purge(&((FwQueue *)handle)->msgq);
  } else {
    k_sem_reset(&((FwQueueSet *)handle)->ready);
  }
  return pdPASS;
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t handle) {
  if (!handle) {
    return 0;
  }
  if (*(FwQueueType *)handle == FwQueueType_Queue) {
    return k_msgq_num_used_get(&((FwQueue *)handle)->msgq);
  }
  UBaseType_t count = 0;
  FwQueueSet *set = handle;
  for (size_t i = 0; i < set->member_count; ++i) {
    count += k_msgq_num_used_get(&set->members[i]->msgq);
  }
  return count;
}

UBaseType_t uxQueueSpacesAvailable(QueueHandle_t queue_handle) {
  FwQueue *queue = queue_handle;
  return queue->length - k_msgq_num_used_get(&queue->msgq);
}

SemaphoreHandle_t fw_semaphore_create(void) {
  return semaphore_create();
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t handle, TickType_t ticks) {
  if (ticks == portMAX_DELAY) {
    semaphore_take(handle);
    return pdTRUE;
  }
  return semaphore_take_with_timeout(handle, ticks_to_milliseconds(ticks)) ? pdTRUE : pdFALSE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t handle) {
  semaphore_give(handle);
  return pdTRUE;
}

BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t handle, BaseType_t *should_context_switch) {
  if (should_context_switch) {
    *should_context_switch = pdFALSE;
  }
  semaphore_give(handle);
  return pdTRUE;
}

#define FW_MAIN_STACK_SIZE 6144
#define FW_BG_STACK_SIZE 4096
#define FW_TIMER_STACK_SIZE 4096

typedef struct {
  struct k_thread thread;
  k_thread_stack_t *stack;
  size_t stack_size;
  TaskFunction_t function;
  void *parameter;
} FwTaskSlot;

K_THREAD_STACK_DEFINE(s_main_stack, FW_MAIN_STACK_SIZE);
K_THREAD_STACK_DEFINE(s_bg_stack, FW_BG_STACK_SIZE);
K_THREAD_STACK_DEFINE(s_timer_stack, FW_TIMER_STACK_SIZE);

static FwTaskSlot s_main_slot = {
  .stack = s_main_stack,
  .stack_size = K_THREAD_STACK_SIZEOF(s_main_stack),
};
static FwTaskSlot s_bg_slot = {
  .stack = s_bg_stack,
  .stack_size = K_THREAD_STACK_SIZEOF(s_bg_stack),
};
static FwTaskSlot s_timer_slot = {
  .stack = s_timer_stack,
  .stack_size = K_THREAD_STACK_SIZEOF(s_timer_stack),
};

static void prv_task_entry(void *arg1, void *arg2, void *arg3) {
  ARG_UNUSED(arg2);
  ARG_UNUSED(arg3);
  FwTaskSlot *slot = arg1;
  const char *name = k_thread_name_get(k_current_get());
  const char *marker_name = strcmp(name, "KernelBG") == 0
                                ? "KernelBackground"
                                : (strcmp(name, "NewTimer") == 0 ? "NewTimers" : name);
  printk("FW_TASK %s up\n", marker_name);
  slot->function(slot->parameter);
}

static FwTaskSlot *prv_slot_for_name(const char *name, int *priority) {
  if (strcmp(name, "KernelMain") == 0) {
    *priority = 5;
    return &s_main_slot;
  }
  if (strcmp(name, "KernelBG") == 0) {
    *priority = 7;
    return &s_bg_slot;
  }
  if (strcmp(name, "NewTimer") == 0) {
    *priority = 2;
    return &s_timer_slot;
  }
  return NULL;
}

BaseType_t xTaskCreateRestricted(const TaskParameters_t *parameters, TaskHandle_t *handle) {
  int priority;
  FwTaskSlot *slot = prv_slot_for_name(parameters->pcName, &priority);
  if (!slot || slot->function) {
    return pdFAIL;
  }
  slot->function = parameters->pvTaskCode;
  slot->parameter = parameters->pvParameters;
  k_tid_t tid = k_thread_create(&slot->thread, slot->stack, slot->stack_size, prv_task_entry, slot,
                                NULL, NULL, priority, 0, K_MSEC(1));
  k_thread_name_set(tid, parameters->pcName);
  *handle = tid;
  return pdPASS;
}

TaskHandle_t xTaskGetCurrentTaskHandle(void) {
  return k_current_get();
}

TaskHandle_t xTaskGetIdleTaskHandle(void) {
  return NULL;
}

const char *pcTaskGetTaskName(TaskHandle_t handle) {
  const char *name = k_thread_name_get(handle);
  return name ? name : "Unknown";
}

UBaseType_t uxTaskGetNumberOfTasks(void) {
  return 3;
}

UBaseType_t uxTaskGetSystemState(TaskStatus_t *statuses, UBaseType_t count,
                                 uint32_t *total_runtime) {
  ARG_UNUSED(statuses);
  ARG_UNUSED(count);
  if (total_runtime) {
    *total_runtime = 0;
  }
  return 0;
}

UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t handle) {
  ARG_UNUSED(handle);
  return UINT16_MAX;
}

void vTaskSuspend(TaskHandle_t handle) {
  k_thread_suspend(handle);
}

void vTaskAllocateMPURegions(TaskHandle_t handle, const MemoryRegion_t *regions) {
  ARG_UNUSED(handle);
  ARG_UNUSED(regions);
}

void vTaskPrioritySet(TaskHandle_t handle, UBaseType_t priority) {
  k_thread_priority_set(handle, priority >= 3 ? 5 : 7);
}

eTaskState eTaskGetState(TaskHandle_t handle) {
  ARG_UNUSED(handle);
  return eReady;
}
