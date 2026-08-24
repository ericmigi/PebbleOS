/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdint.h>

typedef enum {
  RebootReasonCode_EventQueueFull = 22,
} RebootReasonCode;

typedef struct {
  RebootReasonCode code;
  struct {
    uint32_t push_lr;
    uint32_t current_event;
    uint32_t dropped_event;
  } event_queue;
} RebootReason;

void reboot_reason_set(RebootReason *reason);
void reset_due_to_software_failure(void) __attribute__((noreturn));
