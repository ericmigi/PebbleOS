/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "FreeRTOS.h"

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size);
QueueSetHandle_t xQueueCreateSet(UBaseType_t length);
BaseType_t xQueueAddToSet(QueueHandle_t queue, QueueSetHandle_t set);
QueueSetMemberHandle_t xQueueSelectFromSet(QueueSetHandle_t set, TickType_t ticks);
BaseType_t xQueueSendToBack(QueueHandle_t queue, const void *item, TickType_t ticks);
BaseType_t xQueueSendToBackFromISR(QueueHandle_t queue, const void *item,
                                   BaseType_t *should_context_switch);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks);
BaseType_t xQueueSendFromISR(QueueHandle_t queue, const void *item,
                             BaseType_t *should_context_switch);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks);
BaseType_t xQueueReset(QueueHandle_t queue);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue);
UBaseType_t uxQueueSpacesAvailable(QueueHandle_t queue);
