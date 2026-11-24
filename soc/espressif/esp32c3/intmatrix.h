/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/kernel/irq.h>

/**
 * @brief Attach a handler to an interrupt source at runtime.
 *
 * For code that learns its sources only when it runs, such as the radio libraries. The source
 * stays disabled: pbl_irq_enable() and pbl_irq_disable() route it. Handlers run as those bound
 * with PBL_IRQ_CONNECT() do, on the shared line with interrupts masked.
 *
 * @param source Interrupt matrix source, a PBL_SOC_IRQN_* value.
 * @param fn Handler, called as fn(arg).
 * @param arg Argument of @p fn.
 * @return 0, -EINVAL for a bad source, -EBUSY if it has a handler already, -ENOSPC when every
 * slot is taken.
 */
int esp32c3_irq_attach(pbl_irq_t source, void (*fn)(void *arg), void *arg);

/**
 * @brief Disable a source and remove the handler esp32c3_irq_attach() gave it.
 *
 * @param source Interrupt matrix source.
 */
void esp32c3_irq_detach(pbl_irq_t source);
