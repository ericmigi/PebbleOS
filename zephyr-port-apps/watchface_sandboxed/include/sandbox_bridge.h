/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdint.h>

typedef enum {
  SandboxStepAppEntry,
  SandboxStepWindowCreate,
  SandboxStepTextLayerCreate,
  SandboxStepTickSubscribe,
  SandboxStepWindowStackPush,
  SandboxStepEventLoop,
  SandboxStepWaitTick,
  SandboxStepTickHandler,
  SandboxStepRender,
  SandboxStepCount,
} SandboxStep;

int sandbox_syscall_probe(int value);
void sandbox_app_runtime_init(void);
void sandbox_mpu_readback(void);
void sandbox_app_step(SandboxStep step, uintptr_t detail);
void sandbox_app_event_loop(void);
