/* SPDX-License-Identifier: Apache-2.0 */

#include "sandbox.h"
#include "sandbox_syscall.h"

#include <zephyr/arch/arm/custom_sandbox_hooks.h>
#include <zephyr/arch/arm/mpu/arm_mpu.h>
#include <zephyr/linker/linker-defs.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>

#include <cmsis_core.h>
#include <stddef.h>
#include <stdint.h>

#define MPU_MIN_ALIGN 32U
#define SVC_EXC_RETURN_PSP BIT(2)
#define SVC_EXC_RETURN_BASIC_FRAME BIT(4)
#define XPSR_STACK_ALIGN BIT(9)

SandboxAppArena g_sandbox_app_arena __aligned(MPU_MIN_ALIGN);

static struct k_thread *s_app_thread;
static uintptr_t s_stack_start;
static size_t s_stack_size;
static uintptr_t s_arena_start;
static size_t s_arena_size;
static uint8_t s_ram_region;
static uint8_t s_stack_region;
static uint8_t s_code_region;
static uint32_t s_saved_ram_rbar;
static uint32_t s_saved_ram_rlar;
static uint32_t s_saved_stack_rbar;
static uint32_t s_saved_stack_rlar;
static uint32_t s_saved_code_rbar;
static uint32_t s_saved_code_rlar;
static uint32_t s_app_code_rbar;
static uint32_t s_app_code_rlar;
static atomic_t s_sandbox_ready;
static atomic_t s_syscall_active;
static uintptr_t s_app_return_address;

extern const uint8_t __sandbox_syscall_start[];
extern const uint8_t __sandbox_syscall_end[];

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

static bool prv_region_contains(uint32_t rbar, uint32_t rlar, uintptr_t start,
                                size_t size) {
  const uintptr_t base = rbar & MPU_RBAR_BASE_Msk;
  const uintptr_t limit =
      (rlar & MPU_RLAR_LIMIT_Msk) | (MPU_MIN_ALIGN - 1U);

  return size != 0U && (rlar & MPU_RLAR_EN_Msk) != 0U && start >= base &&
         start <= UINTPTR_MAX - (size - 1U) && start + size - 1U <= limit;
}

static bool prv_range_contains(uintptr_t base, size_t extent,
                               const void *buffer, size_t size) {
  const uintptr_t address = (uintptr_t)buffer;

  if (size == 0U) {
    return address >= base && address <= base + extent;
  }
  return address >= base && address <= UINTPTR_MAX - size &&
         address + size <= base + extent;
}

bool sandbox_userspace_buffer_is_valid(const void *buffer, size_t size) {
  return prv_range_contains(s_arena_start, s_arena_size, buffer, size) ||
         prv_range_contains(s_stack_start, s_stack_size, buffer, size);
}

bool sandbox_thread_is_unprivileged(void) {
  return (__get_CONTROL() & CONTROL_nPRIV_Msk) != 0U;
}

static bool __attribute__((noinline, used)) prv_thread_is_privileged(void) {
  return !sandbox_thread_is_unprivileged();
}

void __attribute__((naked, used)) sandbox_syscall_maybe_skip_privilege(void) {
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

static uintptr_t __attribute__((noinline, used)) prv_syscall_exit(void) {
  atomic_clear(&s_syscall_active);
  return s_app_return_address;
}

static void __attribute__((naked, used)) prv_drop_privilege(void) {
  __asm volatile(
      "push {r0, r1}\n"
      "bl prv_syscall_exit\n"
      "mov r12, r0\n"
      "pop {r0, r1}\n"
      "mrs r2, control\n"
      "orr r2, r2, #1\n"
      "msr control, r2\n"
      "isb\n"
      "bx r12\n");
}

void z_arm_custom_thread_restore_hook(struct k_thread *incoming) {
  uint32_t control;

  if (!atomic_get(&s_sandbox_ready)) {
    return;
  }

  control = __get_CONTROL();
  if (incoming == s_app_thread) {
    const uint32_t arena_rbar =
        (s_arena_start & MPU_RBAR_BASE_Msk) | P_RW_U_RW_Msk;
    const uint32_t arena_rlar =
        ((s_arena_start + s_arena_size - 1U) & MPU_RLAR_LIMIT_Msk) |
        (MPU_MAIR_INDEX_SRAM << MPU_RLAR_AttrIndx_Pos) | MPU_RLAR_EN_Msk;
    const uint32_t stack_rbar =
        (s_stack_start & MPU_RBAR_BASE_Msk) | P_RW_U_RW_Msk |
        MPU_RBAR_XN_Msk;
    const uint32_t stack_rlar =
        ((s_stack_start + s_stack_size - 1U) & MPU_RLAR_LIMIT_Msk) |
        (MPU_MAIR_INDEX_SRAM << MPU_RLAR_AttrIndx_Pos) | MPU_RLAR_EN_Msk;

    prv_mpu_region_write(s_code_region, s_app_code_rbar, s_app_code_rlar);
    prv_mpu_region_write(s_ram_region, arena_rbar, arena_rlar);
    prv_mpu_region_write(s_stack_region, stack_rbar, stack_rlar);
    if (atomic_get(&s_syscall_active)) {
      __set_CONTROL(control & ~CONTROL_nPRIV_Msk);
    } else {
      __set_CONTROL(control | CONTROL_nPRIV_Msk);
    }
  } else {
    prv_mpu_region_write(s_stack_region, s_saved_stack_rbar,
                         s_saved_stack_rlar);
    prv_mpu_region_write(s_ram_region, s_saved_ram_rbar, s_saved_ram_rlar);
    prv_mpu_region_write(s_code_region, s_saved_code_rbar, s_saved_code_rlar);
    __set_CONTROL(control & ~CONTROL_nPRIV_Msk);
  }
  __ISB();
}

int z_arm_custom_svc_hook(uint32_t *exc_frame, uint32_t exc_return,
                          uint8_t svc_num) {
  const uintptr_t frame = (uintptr_t)exc_frame;
  const uintptr_t stack_end = s_stack_start + s_stack_size;
  const uintptr_t return_pc = exc_frame[6];
  const uintptr_t svc_pc = return_pc - sizeof(uint16_t);
  const size_t fp_frame_size =
      (exc_return & SVC_EXC_RETURN_BASIC_FRAME) != 0U ? 0U : 18U * sizeof(uint32_t);
  const size_t align_size =
      (exc_frame[7] & XPSR_STACK_ALIGN) != 0U ? sizeof(uint32_t) : 0U;
  const uintptr_t pre_svc_sp =
      frame + 8U * sizeof(uint32_t) + fp_frame_size + align_size;
  const uint16_t svc_instruction = *(const uint16_t *)svc_pc;
  const uintptr_t msp = __get_MSP();

  if (svc_num != 4U || k_current_get() != s_app_thread ||
      (exc_return & SVC_EXC_RETURN_PSP) == 0U ||
      !sandbox_thread_is_unprivileged() ||
      frame < s_stack_start || frame + 8U * sizeof(uint32_t) > stack_end ||
      pre_svc_sp > stack_end ||
      (msp >= s_stack_start && msp < stack_end) ||
      svc_pc < (uintptr_t)__sandbox_syscall_start ||
      svc_pc >= (uintptr_t)__sandbox_syscall_end ||
      svc_instruction != 0xdf04U || atomic_get(&s_syscall_active)) {
    exc_frame[0] = UINT32_MAX;
    return 1;
  }

  s_app_return_address = exc_frame[5];
  exc_frame[5] = (uintptr_t)&prv_drop_privilege;
  atomic_set(&s_syscall_active, 1);
  __set_CONTROL(__get_CONTROL() & ~CONTROL_nPRIV_Msk);
  __ISB();
  return 1;
}

bool sandbox_prepare(struct k_thread *app_thread, uintptr_t stack_start,
                     size_t stack_size) {
  const uint8_t regions =
      (MPU->TYPE & MPU_TYPE_DREGION_Msk) >> MPU_TYPE_DREGION_Pos;
  const uintptr_t arena_start = (uintptr_t)&g_sandbox_app_arena;
  const size_t arena_size = ROUND_UP(
      offsetof(SandboxAppArena, mpu_tail), MPU_MIN_ALIGN);
  bool ram_found = false;
  bool stack_slot_found = false;
  bool code_slot_found = false;
  const uintptr_t code_start =
      ROUND_DOWN((uintptr_t)__rom_region_start, MPU_MIN_ALIGN);
  const uintptr_t code_end =
      ROUND_UP((uintptr_t)__rom_region_end, MPU_MIN_ALIGN);

  if (regions < 4U || code_end <= code_start ||
      (arena_start & (MPU_MIN_ALIGN - 1U)) != 0U ||
      (arena_size & (MPU_MIN_ALIGN - 1U)) != 0U ||
      (stack_start & (MPU_MIN_ALIGN - 1U)) != 0U ||
      (stack_size & (MPU_MIN_ALIGN - 1U)) != 0U) {
    printk("SANDBOX_SETUP_FAIL alignment regions=%u\n", regions);
    return false;
  }

  for (uint8_t i = 0U; i < regions; ++i) {
    uint32_t rbar;
    uint32_t rlar;
    prv_mpu_region_read(i, &rbar, &rlar);
    if (prv_region_contains(rbar, rlar, arena_start, arena_size) &&
        prv_region_contains(rbar, rlar, stack_start, stack_size)) {
      s_ram_region = i;
      s_saved_ram_rbar = rbar;
      s_saved_ram_rlar = rlar;
      ram_found = true;
    }
  }

  for (int i = regions - 1; i >= 0; --i) {
    uint32_t rbar;
    uint32_t rlar;
    prv_mpu_region_read((uint8_t)i, &rbar, &rlar);
    if ((rlar & MPU_RLAR_EN_Msk) == 0U) {
      s_stack_region = (uint8_t)i;
      s_saved_stack_rbar = rbar;
      s_saved_stack_rlar = rlar;
      stack_slot_found = true;
      break;
    }
  }

  for (int i = regions - 1; i >= 0; --i) {
    uint32_t rbar;
    uint32_t rlar;
    prv_mpu_region_read((uint8_t)i, &rbar, &rlar);
    if ((rlar & MPU_RLAR_EN_Msk) == 0U && (uint8_t)i != s_stack_region) {
      s_code_region = (uint8_t)i;
      s_saved_code_rbar = rbar;
      s_saved_code_rlar = rlar;
      code_slot_found = true;
      break;
    }
  }

  if (!ram_found || !stack_slot_found || !code_slot_found ||
      s_stack_region == s_ram_region || s_code_region == s_ram_region) {
    printk("SANDBOX_SETUP_FAIL mpu ram=%u stack=%u code=%u\n", ram_found,
           stack_slot_found, code_slot_found);
    return false;
  }

  s_app_code_rbar = (code_start & MPU_RBAR_BASE_Msk) | P_RO_U_RO_Msk;
  s_app_code_rlar =
      ((code_end - 1U) & MPU_RLAR_LIMIT_Msk) |
      (MPU_MAIR_INDEX_FLASH << MPU_RLAR_AttrIndx_Pos) | MPU_RLAR_EN_Msk;

  s_app_thread = app_thread;
  s_stack_start = stack_start;
  s_stack_size = stack_size;
  s_arena_start = arena_start;
  s_arena_size = arena_size;
  atomic_set(&s_sandbox_ready, 1);
  printk("SANDBOX_MPU arena=%p+%zu stack=%p+%zu code=%p+%zu slots=%u,%u,%u\n",
         (void *)s_arena_start, s_arena_size, (void *)s_stack_start,
         s_stack_size, (void *)code_start, (size_t)(code_end - code_start), s_ram_region,
         s_stack_region, s_code_region);
  return true;
}
