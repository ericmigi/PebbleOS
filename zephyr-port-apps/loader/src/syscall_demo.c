/*
 * Copyright (c) 2026 Core Devices LLC
 * SPDX-License-Identifier: Apache-2.0
 */

#include "syscall_demo.h"

#include <zephyr/arch/arm/custom_sandbox_hooks.h>
#include <zephyr/arch/arm/mpu/arm_mpu.h>
#include <zephyr/kernel.h>
#include <zephyr/linker/linker-defs.h>
#include <zephyr/linker/sections.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>

#include <cmsis_core.h>
#include <stdint.h>

#define APP_STACK_SIZE 2048U
#define MPU_MIN_ALIGN 32U
#define SVC_EXC_RETURN_PSP BIT(2)
#define SYSCALL_DEMO_TIMEOUT_MS 1000

#define DEFINE_SYSCALL(ret_type, function_name, ...)                              \
  ret_type __attribute__((naked, used, section(".text.syscall_text." #function_name))) \
      function_name(__VA_ARGS__) {                                               \
    __asm volatile(                                                              \
        "push {lr}\n"                                                           \
        "bl syscall_demo_maybe_skip_privilege\n"                               \
        ".global syscall_demo_svc_callsite\n"                                  \
        "syscall_demo_svc_callsite:\n"                                         \
        "svc #4\n"                                                              \
        "b __" #function_name "\n");                                           \
  }                                                                              \
  ret_type __attribute__((used)) __##function_name(__VA_ARGS__)

static struct k_thread s_app_thread;
static struct z_thread_stack_element __kstackmem __aligned(MPU_MIN_ALIGN)
    s_app_stack[K_KERNEL_STACK_LEN(APP_STACK_SIZE)];

static atomic_t s_sandbox_ready;
static atomic_t s_syscall_returned;
uintptr_t syscall_demo_app_return_address __attribute__((used));
static uint32_t s_app_stack_start;
static uint32_t s_app_stack_size;
static uint32_t s_saved_sram_rbar;
static uint32_t s_saved_sram_rlar;
static uint32_t s_saved_code_rbar;
static uint32_t s_saved_code_rlar;
static uint32_t s_app_code_rbar;
static uint32_t s_app_code_rlar;
static uint8_t s_sram_region;
static uint8_t s_code_region;
static bool s_code_region_needed;

extern const uint16_t syscall_demo_svc_callsite[];

static void prv_mpu_region_read(uint8_t region, uint32_t *rbar,
                                uint32_t *rlar) {
  MPU->RNR = region;
  *rbar = MPU->RBAR;
  *rlar = MPU->RLAR;
}

static void prv_mpu_region_write(uint8_t region, uint32_t rbar,
                                 uint32_t rlar) {
  __DMB();
  MPU->RNR = region;
  MPU->RBAR = rbar;
  MPU->RLAR = rlar;
  __DSB();
  __ISB();
}

static bool prv_region_contains(uint32_t rbar, uint32_t rlar, uint32_t start,
                                uint32_t size) {
  const uint32_t base = rbar & MPU_RBAR_BASE_Msk;
  const uint32_t limit = (rlar & MPU_RLAR_LIMIT_Msk) | (MPU_MIN_ALIGN - 1U);

  return (rlar & MPU_RLAR_EN_Msk) != 0U && start >= base &&
         (start + size - 1U) <= limit;
}

static bool prv_region_is_user_executable(uint32_t rbar, uint32_t rlar,
                                          uint32_t start, uint32_t size) {
  const uint32_t ap = (rbar & MPU_RBAR_AP_Msk) >> MPU_RBAR_AP_Pos;

  return prv_region_contains(rbar, rlar, start, size) &&
         (rbar & MPU_RBAR_XN_Msk) == 0U &&
         (ap == P_RW_U_RW || ap == P_RO_U_RO);
}

void z_arm_custom_thread_restore_hook(struct k_thread *incoming) {
  uint32_t control = __get_CONTROL();

  if (!atomic_get(&s_sandbox_ready)) {
    return;
  }

  if (incoming == &s_app_thread) {
    const uint32_t rbar =
        (s_app_stack_start & MPU_RBAR_BASE_Msk) | P_RW_U_RW_Msk |
        MPU_RBAR_XN_Msk;
    const uint32_t rlar =
        ((s_app_stack_start + s_app_stack_size - 1U) & MPU_RLAR_LIMIT_Msk) |
        (MPU_MAIR_INDEX_SRAM << MPU_RLAR_AttrIndx_Pos) | MPU_RLAR_EN_Msk;

    if (s_code_region_needed) {
      prv_mpu_region_write(s_code_region, s_app_code_rbar, s_app_code_rlar);
    }
    prv_mpu_region_write(s_sram_region, rbar, rlar);
    __set_CONTROL(control | CONTROL_nPRIV_Msk);
  } else {
    prv_mpu_region_write(s_sram_region, s_saved_sram_rbar,
                         s_saved_sram_rlar);
    if (s_code_region_needed) {
      prv_mpu_region_write(s_code_region, s_saved_code_rbar,
                           s_saved_code_rlar);
    }
    __set_CONTROL(control & ~CONTROL_nPRIV_Msk);
  }
  __ISB();
}

static void __attribute__((naked, used)) syscall_demo_drop_privilege(void) {
  __asm volatile(
      "ldr r12, =syscall_demo_app_return_address\n"
      "ldr r12, [r12]\n"
      "mrs r2, control\n"
      "orr r2, r2, #1\n"
      "msr control, r2\n"
      "isb\n"
      "bx r12\n");
}

int z_arm_custom_svc_hook(uint32_t *exc_frame, uint32_t exc_return,
                          uint8_t svc_num) {
  const uint32_t stack_end = s_app_stack_start + s_app_stack_size;
  const uint32_t expected_pc =
      (uint32_t)(uintptr_t)syscall_demo_svc_callsite + sizeof(uint16_t);
  const uint32_t msp = __get_MSP();

  if (svc_num != 4U || k_current_get() != &s_app_thread ||
      (exc_return & SVC_EXC_RETURN_PSP) == 0U ||
      (__get_CONTROL() & CONTROL_nPRIV_Msk) == 0U ||
      (uint32_t)exc_frame < s_app_stack_start ||
      ((uint32_t)exc_frame + 8U * sizeof(uint32_t)) > stack_end ||
      (msp >= s_app_stack_start && msp < stack_end) ||
      exc_frame[6] != expected_pc) {
    exc_frame[0] = UINT32_MAX;
    return 1;
  }

  syscall_demo_app_return_address = exc_frame[5];
  exc_frame[5] = (uint32_t)(uintptr_t)&syscall_demo_drop_privilege;

  uint32_t control = __get_CONTROL();
  __set_CONTROL(control & ~CONTROL_nPRIV_Msk);
  __ISB();
  return 1;
}

static bool __attribute__((noinline, used)) prv_thread_is_privileged(void) {
  return (__get_CONTROL() & CONTROL_nPRIV_Msk) == 0U;
}

void __attribute__((naked, used)) syscall_demo_maybe_skip_privilege(void) {
  __asm volatile(
      "push {r0-r3, lr}\n"
      "bl prv_thread_is_privileged\n"
      "cmp r0, #1\n"
      "pop {r0-r3, lr}\n"
      "it eq\n"
      "addeq lr, #2\n"
      "mov ip, lr\n"
      "pop {lr}\n"
      "push {ip}\n"
      "pop {pc}\n");
}

DEFINE_SYSCALL(int, syscall_demo_double, int value) {
  if (value == 42) {
    atomic_set(&s_syscall_returned, 1);
  }
  return value * 2;
}

static void prv_app_entry(void *p1, void *p2, void *p3) {
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  const int value = syscall_demo_double(21);
  (void)syscall_demo_double(value);
  for (;;) {
    __WFE();
  }
}

static bool prv_sandbox_prepare(void) {
  const uint8_t regions =
      (MPU->TYPE & MPU_TYPE_DREGION_Msk) >> MPU_TYPE_DREGION_Pos;
  const uint32_t code_start =
      ROUND_DOWN((uint32_t)__text_region_start, MPU_MIN_ALIGN);
  const uint32_t code_end =
      ROUND_UP((uint32_t)__text_region_end, MPU_MIN_ALIGN);
  bool sram_found = false;
  bool code_slot_found = false;
  bool code_access_found = false;

  if (regions < 8U ||
      (s_app_stack_start & (MPU_MIN_ALIGN - 1U)) != 0U ||
      (s_app_stack_size & (MPU_MIN_ALIGN - 1U)) != 0U ||
      code_end <= code_start ||
      !IN_RANGE((uint32_t)prv_app_entry, code_start, code_end - 1U)) {
    return false;
  }

  for (uint8_t i = 0U; i < regions; ++i) {
    uint32_t rbar;
    uint32_t rlar;
    prv_mpu_region_read(i, &rbar, &rlar);
    if (prv_region_is_user_executable(rbar, rlar, code_start,
                                      code_end - code_start)) {
      code_access_found = true;
    }
    if (prv_region_contains(rbar, rlar, s_app_stack_start,
                            s_app_stack_size)) {
      s_sram_region = i;
      s_saved_sram_rbar = rbar;
      s_saved_sram_rlar = rlar;
      sram_found = true;
    }
  }
  if (!sram_found) {
    return false;
  }

  for (int i = regions - 1; !code_access_found && i >= 0; --i) {
    uint32_t rbar;
    uint32_t rlar;
    prv_mpu_region_read((uint8_t)i, &rbar, &rlar);
    if ((rlar & MPU_RLAR_EN_Msk) == 0U) {
      s_code_region = (uint8_t)i;
      s_saved_code_rbar = rbar;
      s_saved_code_rlar = rlar;
      code_slot_found = true;
      break;
    }
  }
  if (!code_access_found && !code_slot_found) {
    return false;
  }

  s_code_region_needed = !code_access_found;
  if (s_code_region_needed) {
    s_app_code_rbar = (code_start & MPU_RBAR_BASE_Msk) | P_RO_U_RO_Msk;
    s_app_code_rlar =
        ((code_end - 1U) & MPU_RLAR_LIMIT_Msk) |
        (MPU_MAIR_INDEX_FLASH << MPU_RLAR_AttrIndx_Pos) | MPU_RLAR_EN_Msk;
  }
  return true;
}

bool syscall_demo_run(void) {
  s_app_stack_start = (uint32_t)K_KERNEL_STACK_BUFFER(s_app_stack);
  s_app_stack_size = K_KERNEL_STACK_SIZEOF(s_app_stack);

  k_thread_create(&s_app_thread, s_app_stack, s_app_stack_size, prv_app_entry,
                  NULL, NULL, NULL, 5, 0, K_FOREVER);
  k_thread_name_set(&s_app_thread, "pbw-syscall-demo");

  if (!prv_sandbox_prepare()) {
    printk("SYSCALL_FAIL: MPU setup\n");
    return false;
  }

  atomic_set(&s_sandbox_ready, 1);
  k_thread_start(&s_app_thread);

  for (int elapsed = 0; elapsed < SYSCALL_DEMO_TIMEOUT_MS; ++elapsed) {
    if (atomic_get(&s_syscall_returned)) {
      printk("SYSCALL: DEFINE_SYSCALL svc#4 returned 42\n");
      return true;
    }
    k_sleep(K_MSEC(1));
  }

  printk("SYSCALL_FAIL: timeout\n");
  return false;
}
