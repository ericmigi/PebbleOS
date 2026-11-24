/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdbool.h>
#include <stdint.h>

#include <pbl/kernel/compiler.h>

#include <pbl_arch_irq.h>

/*
 * Without the A extension the compiler turns read-modify-write atomics into these calls; loads
 * and stores of up to 32 bits stay inline. On a single core, masking interrupts makes them
 * atomic, from threads, ISRs and before the kernel runs alike. libatomic would take a lock
 * instead, which an ISR could spin on forever.
 */

#define ATOMIC_RMW(name, n, type, expr)                                                   \
  PBL_USED type __atomic_fetch_##name##_##n(volatile void *ptr, type val, int memorder) { \
    volatile type *p = ptr;                                                               \
    unsigned int key = arch_irq_disable_all();                                            \
    type old = *p;                                                                        \
    *p = (expr);                                                                          \
    arch_irq_restore_all(key);                                                            \
    (void)memorder;                                                                       \
    return old;                                                                           \
  }                                                                                       \
  PBL_USED type __atomic_##name##_fetch_##n(volatile void *ptr, type val, int memorder) { \
    volatile type *p = ptr;                                                               \
    unsigned int key = arch_irq_disable_all();                                            \
    type old = *p;                                                                        \
    type new = (expr);                                                                    \
    *p = new;                                                                             \
    arch_irq_restore_all(key);                                                            \
    (void)memorder;                                                                       \
    return new;                                                                           \
  }

#define ATOMIC_ALL(n, type)                                                                     \
  PBL_USED type __atomic_exchange_##n(volatile void *ptr, type val, int memorder) {             \
    volatile type *p = ptr;                                                                     \
    unsigned int key = arch_irq_disable_all();                                                  \
    type old = *p;                                                                              \
    *p = val;                                                                                   \
    arch_irq_restore_all(key);                                                                  \
    (void)memorder;                                                                             \
    return old;                                                                                 \
  }                                                                                             \
  PBL_USED bool __atomic_compare_exchange_##n(volatile void *ptr, void *expected, type desired, \
                                              bool weak, int success, int failure) {            \
    volatile type *p = ptr;                                                                     \
    type *e = expected;                                                                         \
    unsigned int key = arch_irq_disable_all();                                                  \
    type cur = *p;                                                                              \
    bool ok = cur == *e;                                                                        \
    if (ok) {                                                                                   \
      *p = desired;                                                                             \
    } else {                                                                                    \
      *e = cur;                                                                                 \
    }                                                                                           \
    arch_irq_restore_all(key);                                                                  \
    (void)weak;                                                                                 \
    (void)success;                                                                              \
    (void)failure;                                                                              \
    return ok;                                                                                  \
  }                                                                                             \
  ATOMIC_RMW(add, n, type, old + val)                                                           \
  ATOMIC_RMW(sub, n, type, old - val)                                                           \
  ATOMIC_RMW(and, n, type, old &val)                                                            \
  ATOMIC_RMW(or, n, type, old | val)                                                            \
  ATOMIC_RMW(xor, n, type, old ^ val)                                                           \
  ATOMIC_RMW(nand, n, type, ~(old & val))

ATOMIC_ALL(1, uint8_t)
ATOMIC_ALL(2, uint16_t)
ATOMIC_ALL(4, unsigned int)

// atomic_flag_test_and_set(); the flag is a byte
PBL_USED bool __atomic_test_and_set(volatile void *ptr, int memorder) {
  volatile uint8_t *p = ptr;
  unsigned int key = arch_irq_disable_all();
  bool old = *p != 0U;
  *p = 1U;
  arch_irq_restore_all(key);
  (void)memorder;
  return old;
}
