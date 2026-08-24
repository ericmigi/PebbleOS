/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

typedef enum PebbleTask {
  PebbleTask_KernelMain,
  PebbleTask_Unknown,
} PebbleTask;

PebbleTask pebble_task_get_current(void);
