/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <assert.h>
#include <limits.h>
#include <string.h>

#include <zephyr/arch/cpu.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>

#include "nimble/nimble_npl.h"

static k_timeout_t prv_timeout(ble_npl_time_t ticks) {
  return ticks == BLE_NPL_TIME_FOREVER ? K_FOREVER : K_TICKS(ticks);
}

bool ble_npl_os_started(void) {
  return true;
}

void *ble_npl_get_current_task_id(void) {
  return k_current_get();
}

void ble_npl_eventq_init(struct ble_npl_eventq *evq) {
  k_queue_init(&evq->queue);
}

struct ble_npl_event *ble_npl_eventq_get(struct ble_npl_eventq *evq,
                                         ble_npl_time_t tmo) {
  struct ble_npl_event *ev = k_queue_get(&evq->queue, prv_timeout(tmo));
  if (ev != NULL) {
    atomic_clear(&ev->queued);
  }
  return ev;
}

void ble_npl_eventq_put(struct ble_npl_eventq *evq, struct ble_npl_event *ev) {
  if (atomic_cas(&ev->queued, 0, 1)) {
    k_queue_append(&evq->queue, ev);
  }
}

void ble_npl_eventq_remove(struct ble_npl_eventq *evq, struct ble_npl_event *ev) {
  if (atomic_get(&ev->queued) && k_queue_remove(&evq->queue, ev)) {
    atomic_clear(&ev->queued);
  }
}

void ble_npl_event_init(struct ble_npl_event *ev, ble_npl_event_fn *fn, void *arg) {
  memset(ev, 0, sizeof(*ev));
  ev->fn = fn;
  ev->arg = arg;
}

bool ble_npl_event_is_queued(struct ble_npl_event *ev) {
  return atomic_get(&ev->queued) != 0;
}

void *ble_npl_event_get_arg(struct ble_npl_event *ev) {
  return ev->arg;
}

void ble_npl_event_set_arg(struct ble_npl_event *ev, void *arg) {
  ev->arg = arg;
}

bool ble_npl_eventq_is_empty(struct ble_npl_eventq *evq) {
  return k_queue_is_empty(&evq->queue);
}

void ble_npl_event_run(struct ble_npl_event *ev) {
  ev->fn(ev);
}

ble_npl_error_t ble_npl_mutex_init(struct ble_npl_mutex *mu) {
  if (mu == NULL) {
    return BLE_NPL_INVALID_PARAM;
  }
  return k_mutex_init(&mu->mutex) == 0 ? BLE_NPL_OK : BLE_NPL_BAD_MUTEX;
}

ble_npl_error_t ble_npl_mutex_pend(struct ble_npl_mutex *mu, ble_npl_time_t timeout) {
  if (mu == NULL) {
    return BLE_NPL_INVALID_PARAM;
  }
  if (k_is_in_isr()) {
    return BLE_NPL_ERR_IN_ISR;
  }
  return k_mutex_lock(&mu->mutex, prv_timeout(timeout)) == 0 ? BLE_NPL_OK : BLE_NPL_TIMEOUT;
}

ble_npl_error_t ble_npl_mutex_release(struct ble_npl_mutex *mu) {
  if (mu == NULL) {
    return BLE_NPL_INVALID_PARAM;
  }
  return k_mutex_unlock(&mu->mutex) == 0 ? BLE_NPL_OK : BLE_NPL_BAD_MUTEX;
}

ble_npl_error_t ble_npl_sem_init(struct ble_npl_sem *sem, uint16_t tokens) {
  if (sem == NULL) {
    return BLE_NPL_INVALID_PARAM;
  }
  return k_sem_init(&sem->sem, tokens, 128) == 0 ? BLE_NPL_OK : BLE_NPL_INVALID_PARAM;
}

ble_npl_error_t ble_npl_sem_pend(struct ble_npl_sem *sem, ble_npl_time_t timeout) {
  if (sem == NULL) {
    return BLE_NPL_INVALID_PARAM;
  }
  if (k_is_in_isr() && timeout != 0) {
    return BLE_NPL_ERR_IN_ISR;
  }
  return k_sem_take(&sem->sem, prv_timeout(timeout)) == 0 ? BLE_NPL_OK : BLE_NPL_TIMEOUT;
}

ble_npl_error_t ble_npl_sem_release(struct ble_npl_sem *sem) {
  if (sem == NULL) {
    return BLE_NPL_INVALID_PARAM;
  }
  k_sem_give(&sem->sem);
  return BLE_NPL_OK;
}

uint16_t ble_npl_sem_get_count(struct ble_npl_sem *sem) {
  return k_sem_count_get(&sem->sem);
}

static void prv_callout_work(struct k_work *work) {
  struct k_work_delayable *delayable = k_work_delayable_from_work(work);
  struct ble_npl_callout *co = CONTAINER_OF(delayable, struct ble_npl_callout, work);

  atomic_clear(&co->active);
  if (co->evq != NULL) {
    ble_npl_eventq_put(co->evq, &co->ev);
  } else {
    ble_npl_event_run(&co->ev);
  }
}

void ble_npl_callout_init(struct ble_npl_callout *co, struct ble_npl_eventq *evq,
                          ble_npl_event_fn *ev_cb, void *ev_arg) {
  memset(co, 0, sizeof(*co));
  k_work_init_delayable(&co->work, prv_callout_work);
  co->evq = evq;
  ble_npl_event_init(&co->ev, ev_cb, ev_arg);
}

ble_npl_error_t ble_npl_callout_reset(struct ble_npl_callout *co, ble_npl_time_t ticks) {
  co->ticks = ble_npl_time_get() + ticks;
  atomic_set(&co->active, 1);
  return k_work_reschedule(&co->work, K_TICKS(ticks)) < 0 ? BLE_NPL_ERROR : BLE_NPL_OK;
}

void ble_npl_callout_stop(struct ble_npl_callout *co) {
  (void)k_work_cancel_delayable(&co->work);
  atomic_clear(&co->active);
}

bool ble_npl_callout_is_active(struct ble_npl_callout *co) {
  return atomic_get(&co->active) != 0;
}

ble_npl_time_t ble_npl_callout_get_ticks(struct ble_npl_callout *co) {
  return co->ticks;
}

ble_npl_time_t ble_npl_callout_remaining_ticks(struct ble_npl_callout *co,
                                               ble_npl_time_t now) {
  ble_npl_stime_t remaining = (ble_npl_stime_t)(co->ticks - now);
  return ble_npl_callout_is_active(co) && remaining > 0 ? (ble_npl_time_t)remaining : 0;
}

void ble_npl_callout_set_arg(struct ble_npl_callout *co, void *arg) {
  co->ev.arg = arg;
}

ble_npl_time_t ble_npl_time_get(void) {
  return (ble_npl_time_t)k_uptime_ticks();
}

ble_npl_error_t ble_npl_time_ms_to_ticks(uint32_t ms, ble_npl_time_t *out_ticks) {
  uint64_t ticks;

  if (out_ticks == NULL) {
    return BLE_NPL_INVALID_PARAM;
  }
  ticks = ((uint64_t)ms * CONFIG_SYS_CLOCK_TICKS_PER_SEC) / 1000U;
  if (ticks > UINT32_MAX) {
    return BLE_NPL_EINVAL;
  }
  *out_ticks = (ble_npl_time_t)ticks;
  return BLE_NPL_OK;
}

ble_npl_error_t ble_npl_time_ticks_to_ms(ble_npl_time_t ticks, uint32_t *out_ms) {
  uint64_t ms;

  if (out_ms == NULL) {
    return BLE_NPL_INVALID_PARAM;
  }
  ms = ((uint64_t)ticks * 1000U) / CONFIG_SYS_CLOCK_TICKS_PER_SEC;
  if (ms > UINT32_MAX) {
    return BLE_NPL_EINVAL;
  }
  *out_ms = (uint32_t)ms;
  return BLE_NPL_OK;
}

ble_npl_time_t ble_npl_time_ms_to_ticks32(uint32_t ms) {
  ble_npl_time_t ticks;
  return ble_npl_time_ms_to_ticks(ms, &ticks) == BLE_NPL_OK ? ticks : UINT32_MAX;
}

uint32_t ble_npl_time_ticks_to_ms32(ble_npl_time_t ticks) {
  uint32_t ms;
  return ble_npl_time_ticks_to_ms(ticks, &ms) == BLE_NPL_OK ? ms : UINT32_MAX;
}

void ble_npl_time_delay(ble_npl_time_t ticks) {
  k_sleep(K_TICKS(ticks));
}

uint32_t ble_npl_hw_enter_critical(void) {
  return irq_lock();
}

void ble_npl_hw_exit_critical(uint32_t ctx) {
  irq_unlock(ctx);
}

bool ble_npl_hw_is_in_critical(void) {
  unsigned int key = irq_lock();
  bool locked = !arch_irq_unlocked(key);
  irq_unlock(key);
  return locked;
}
