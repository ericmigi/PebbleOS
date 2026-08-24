/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

struct pebble_semaphore_t;
typedef struct pebble_semaphore_t PebbleSemaphore;

//! Creates an initially unavailable binary semaphore.
//! @return NULL if allocation failed, a semaphore otherwise.
PebbleSemaphore *semaphore_create(void);

void semaphore_destroy(PebbleSemaphore *handle);

//! Waits indefinitely for the semaphore.
void semaphore_take(PebbleSemaphore *handle);

bool semaphore_take_with_timeout(PebbleSemaphore *handle, uint32_t timeout_ms);

//! Gives the semaphore from thread context.
void semaphore_give(PebbleSemaphore *handle);
