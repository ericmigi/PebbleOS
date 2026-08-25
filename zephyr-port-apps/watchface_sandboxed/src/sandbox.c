/* SPDX-License-Identifier: Apache-2.0 */

#include "sandbox.h"
#include "sandbox_syscall.h"

#include <zephyr/arch/arm/custom_sandbox_hooks.h>
#include <zephyr/arch/arm/mpu/arm_mpu.h>
#include <zephyr/fatal.h>
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
static bool s_code_region_needed;
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

// Raw region write with no barriers; only valid while the MPU is disabled
// (see prv_mpu_batch_begin/end). Reprogramming several regions with the MPU
// enabled forces a pipeline refetch after each write under a transiently
// inconsistent map, which faults; disabling the MPU for the batch uses the
// privileged default map (all code executable) until the full new map is in.
static void prv_mpu_region_write_raw(uint8_t region, uint32_t rbar,
                                     uint32_t rlar) {
  MPU->RNR = region;
  MPU->RBAR = rbar;
  MPU->RLAR = rlar;
}

static uint32_t prv_mpu_batch_begin(void) {
  const uint32_t ctrl = MPU->CTRL;
  __DMB();
  MPU->CTRL = 0U;
  __DSB();
  __ISB();
  return ctrl;
}

static void prv_mpu_batch_end(uint32_t ctrl) {
  __DSB();
  MPU->CTRL = ctrl;
  __DSB();
  __ISB();
}

static void prv_mpu_dump(void) {
  const uint8_t regions =
      (MPU->TYPE & MPU_TYPE_DREGION_Msk) >> MPU_TYPE_DREGION_Pos;

  printk("SANDBOX_FAULT_MPU ctrl=0x%08x type=0x%08x mair0=0x%08x "
         "mair1=0x%08x\n",
         MPU->CTRL, MPU->TYPE, MPU->MAIR0, MPU->MAIR1);
  for (uint8_t i = 0U; i < regions; ++i) {
    uint32_t rbar;
    uint32_t rlar;
    prv_mpu_region_read(i, &rbar, &rlar);
    printk("SANDBOX_FAULT_MPU region=%u rbar=0x%08x rlar=0x%08x "
           "base=0x%08x limit=0x%08x ap=%u xn=%u attr=%u en=%u\n",
           i, rbar, rlar, (uint32_t)(rbar & MPU_RBAR_BASE_Msk),
           (uint32_t)((rlar & MPU_RLAR_LIMIT_Msk) | (MPU_MIN_ALIGN - 1U)),
           (unsigned int)((rbar & MPU_RBAR_AP_Msk) >> MPU_RBAR_AP_Pos),
           (unsigned int)((rbar & MPU_RBAR_XN_Msk) != 0U),
           (unsigned int)((rlar & MPU_RLAR_AttrIndx_Msk) >>
                          MPU_RLAR_AttrIndx_Pos),
           (unsigned int)((rlar & MPU_RLAR_EN_Msk) != 0U));
  }
}

void k_sys_fatal_error_handler(unsigned int reason,
                               const struct arch_esf *esf) {
  const uint32_t cfsr = SCB->CFSR;
  const uint32_t pc = esf ? esf->basic.pc : 0U;
  const uint32_t lr = esf ? esf->basic.lr : 0U;
  const uint32_t xpsr = esf ? esf->basic.xpsr : 0U;

  printk("SANDBOX_FATAL reason=%u thread=%p control=0x%08x msp=0x%08x "
         "psp=0x%08x psplim=0x%08x\n",
         reason, k_current_get(), __get_CONTROL(), __get_MSP(), __get_PSP(),
         __get_PSPLIM());
  printk("SANDBOX_FAULT cfsr=0x%08x hfsr=0x%08x mmfar=0x%08x "
         "mmfar_valid=%u bfar=0x%08x bfar_valid=%u pc=0x%08x lr=0x%08x "
         "xpsr=0x%08x\n",
         cfsr, SCB->HFSR, SCB->MMFAR,
         (cfsr & SCB_CFSR_MMARVALID_Msk) != 0U, SCB->BFAR,
         (cfsr & SCB_CFSR_BFARVALID_Msk) != 0U, pc, lr, xpsr);
  prv_mpu_dump();

  if (reason == K_ERR_KERNEL_PANIC || k_current_get() != s_app_thread) {
    k_fatal_halt(reason);
  }
}

static bool prv_region_contains(uint32_t rbar, uint32_t rlar, uintptr_t start,
                                size_t size) {
  const uintptr_t base = rbar & MPU_RBAR_BASE_Msk;
  const uintptr_t limit =
      (rlar & MPU_RLAR_LIMIT_Msk) | (MPU_MIN_ALIGN - 1U);

  return size != 0U && (rlar & MPU_RLAR_EN_Msk) != 0U && start >= base &&
         start <= UINTPTR_MAX - (size - 1U) && start + size - 1U <= limit;
}

static bool prv_region_is_user_executable(uint32_t rbar, uint32_t rlar,
                                          uintptr_t start, size_t size) {
  const uint32_t ap =
      (rbar & MPU_RBAR_AP_Msk) >> MPU_RBAR_AP_Pos;

  return prv_region_contains(rbar, rlar, start, size) &&
         (rbar & MPU_RBAR_XN_Msk) == 0U &&
         (ap == P_RW_U_RW || ap == P_RO_U_RO);
}

void sandbox_dump_active_mpu(void) {
  uint32_t rbar;
  uint32_t rlar;

  prv_mpu_region_read(s_ram_region, &rbar, &rlar);
  printk("SANDBOX_MPU_ACTIVE arena_region=%u rbar=0x%08x rlar=0x%08x "
         "ap=%u xn=%u user_exec=%u control=0x%08x\n",
         s_ram_region, rbar, rlar,
         (unsigned int)((rbar & MPU_RBAR_AP_Msk) >> MPU_RBAR_AP_Pos),
         (unsigned int)((rbar & MPU_RBAR_XN_Msk) != 0U),
         prv_region_is_user_executable(rbar, rlar, s_arena_start,
                                       s_arena_size),
         __get_CONTROL());
}

static bool prv_ranges_overlap(uintptr_t first_start, size_t first_size,
                               uintptr_t second_start, size_t second_size) {
  return first_size != 0U && second_size != 0U &&
         first_start <= UINTPTR_MAX - first_size &&
         second_start <= UINTPTR_MAX - second_size &&
         first_start < second_start + second_size &&
         second_start < first_start + first_size;
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
  if (!atomic_get(&s_sandbox_ready)) {
    return;
  }

  const uint32_t control = __get_CONTROL();
  if (incoming == s_app_thread) {
    const uint32_t arena_rbar =
        ((s_arena_start & MPU_RBAR_BASE_Msk) | P_RW_U_RW_Msk) &
        ~MPU_RBAR_XN_Msk;
    const uint32_t arena_rlar =
        ((s_arena_start + s_arena_size - 1U) & MPU_RLAR_LIMIT_Msk) |
        (MPU_MAIR_INDEX_SRAM << MPU_RLAR_AttrIndx_Pos) | MPU_RLAR_EN_Msk;
    const uint32_t stack_rbar =
        (s_stack_start & MPU_RBAR_BASE_Msk) | P_RW_U_RW_Msk |
        MPU_RBAR_XN_Msk;
    const uint32_t stack_rlar =
        ((s_stack_start + s_stack_size - 1U) & MPU_RLAR_LIMIT_Msk) |
        (MPU_MAIR_INDEX_SRAM << MPU_RLAR_AttrIndx_Pos) | MPU_RLAR_EN_Msk;

    const uint32_t ctrl = prv_mpu_batch_begin();
    if (s_code_region_needed) {
      prv_mpu_region_write_raw(s_code_region, s_app_code_rbar, s_app_code_rlar);
    }
    prv_mpu_region_write_raw(s_ram_region, arena_rbar, arena_rlar);
    prv_mpu_region_write_raw(s_stack_region, stack_rbar, stack_rlar);
    prv_mpu_batch_end(ctrl);
    if (atomic_get(&s_syscall_active)) {
      __set_CONTROL(control & ~CONTROL_nPRIV_Msk);
    } else {
      __set_CONTROL(control | CONTROL_nPRIV_Msk);
    }
  } else {
    const uint32_t ctrl = prv_mpu_batch_begin();
    prv_mpu_region_write_raw(s_stack_region, s_saved_stack_rbar,
                             s_saved_stack_rlar);
    prv_mpu_region_write_raw(s_ram_region, s_saved_ram_rbar, s_saved_ram_rlar);
    if (s_code_region_needed) {
      prv_mpu_region_write_raw(s_code_region, s_saved_code_rbar,
                               s_saved_code_rlar);
    }
    prv_mpu_batch_end(ctrl);
    __set_CONTROL(control & ~CONTROL_nPRIV_Msk);
  }
  __ISB();
}

int z_arm_custom_svc_hook(uint32_t *exc_frame, uint32_t exc_return,
                          uint8_t svc_num) {
  uintptr_t frame;
  uintptr_t stack_end;
  uintptr_t return_pc;
  uintptr_t svc_pc;
  uintptr_t msp;
  size_t frame_size;

  if (!atomic_get(&s_sandbox_ready)) {
    return 0;
  }
  if (svc_num != 4U || k_current_get() != s_app_thread) {
    return 0;
  }

  frame = (uintptr_t)exc_frame;
  stack_end = s_stack_start + s_stack_size;
  msp = __get_MSP();
  if ((exc_return & SVC_EXC_RETURN_PSP) == 0U ||
      !sandbox_thread_is_unprivileged() || frame < s_stack_start ||
      frame > stack_end || stack_end - frame < 8U * sizeof(uint32_t) ||
      (msp >= s_stack_start && msp < stack_end)) {
    return 0;
  }

  frame_size = 8U * sizeof(uint32_t);
  if ((exc_return & SVC_EXC_RETURN_BASIC_FRAME) == 0U) {
    frame_size += 18U * sizeof(uint32_t);
  }
  if ((exc_frame[7] & XPSR_STACK_ALIGN) != 0U) {
    frame_size += sizeof(uint32_t);
  }
  return_pc = exc_frame[6];
  if (frame_size > stack_end - frame || return_pc < sizeof(uint16_t)) {
    exc_frame[0] = UINT32_MAX;
    return 1;
  }

  svc_pc = return_pc - sizeof(uint16_t);
  if (svc_pc < (uintptr_t)__sandbox_syscall_start ||
      svc_pc >= (uintptr_t)__sandbox_syscall_end ||
      *(const uint16_t *)svc_pc != 0xdf04U ||
      atomic_get(&s_syscall_active)) {
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

void sandbox_arm(void) {
  atomic_set(&s_sandbox_ready, 1);
}

void sandbox_disarm(void) {
  atomic_clear(&s_sandbox_ready);
  atomic_clear(&s_syscall_active);
  s_app_thread = NULL;
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
  bool code_access_found = false;
  const uintptr_t code_start =
      ROUND_DOWN((uintptr_t)__rom_region_start, MPU_MIN_ALIGN);
  const uintptr_t code_end =
      ROUND_UP((uintptr_t)__rom_region_end, MPU_MIN_ALIGN);

  if (regions < 4U) {
    printk("SANDBOX_SETUP_FAIL %s:%d region_count=%u required=4\n",
           __FILE_NAME__, __LINE__, regions);
    return false;
  }
  if (code_end <= code_start ||
      (arena_start & (MPU_MIN_ALIGN - 1U)) != 0U ||
      (arena_size & (MPU_MIN_ALIGN - 1U)) != 0U ||
      (stack_start & (MPU_MIN_ALIGN - 1U)) != 0U ||
      (stack_size & (MPU_MIN_ALIGN - 1U)) != 0U) {
    printk("SANDBOX_SETUP_FAIL %s:%d alignment code=%p..%p "
           "arena=%p+%zu stack=%p+%zu align=%u\n",
           __FILE_NAME__, __LINE__, (void *)code_start, (void *)code_end,
           (void *)arena_start, arena_size, (void *)stack_start, stack_size,
           MPU_MIN_ALIGN);
    return false;
  }
  if (prv_ranges_overlap(arena_start, arena_size, stack_start, stack_size)) {
    printk("SANDBOX_SETUP_FAIL %s:%d arena_stack_overlap arena=%p+%zu "
           "stack=%p+%zu\n", __FILE_NAME__, __LINE__, (void *)arena_start,
           arena_size, (void *)stack_start, stack_size);
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
    if (prv_region_is_user_executable(rbar, rlar, code_start,
                                      code_end - code_start)) {
      code_access_found = true;
      printk("SANDBOX_MPU_CODE_EXISTING region=%u rbar=0x%08x rlar=0x%08x\n",
             i, rbar, rlar);
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

  for (int i = regions - 1; !code_access_found && i >= 0; --i) {
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

  if (!ram_found || !stack_slot_found ||
      (!code_access_found && !code_slot_found) ||
      s_stack_region == s_ram_region ||
      (!code_access_found && s_code_region == s_ram_region)) {
    printk("SANDBOX_SETUP_FAIL %s:%d slots ram_found=%u ram=%u "
           "stack_found=%u stack=%u code_access=%u code_slot=%u code=%u\n",
           __FILE_NAME__, __LINE__, ram_found, s_ram_region,
           stack_slot_found, s_stack_region, code_access_found,
           code_slot_found, s_code_region);
    return false;
  }

  s_code_region_needed = !code_access_found;
  if (s_code_region_needed) {
    // Firmware code must be user-executable: RO for priv+user, and XN cleared
    // (the arena region clears XN the same way). Without clearing XN the region
    // is execute-never, so fetching firmware code faults with an instruction
    // access violation.
    s_app_code_rbar =
        ((code_start & MPU_RBAR_BASE_Msk) | P_RO_U_RO_Msk) & ~MPU_RBAR_XN_Msk;
    s_app_code_rlar =
        ((code_end - 1U) & MPU_RLAR_LIMIT_Msk) |
        (MPU_MAIR_INDEX_FLASH << MPU_RLAR_AttrIndx_Pos) | MPU_RLAR_EN_Msk;
  }

  s_app_thread = app_thread;
  s_stack_start = stack_start;
  s_stack_size = stack_size;
  s_arena_start = arena_start;
  s_arena_size = arena_size;
  printk("SANDBOX_MPU arena=%p+%zu stack=%p+%zu code=%p+%zu "
         "slots=ram:%u stack:%u code:%s%u\n",
         (void *)s_arena_start, s_arena_size, (void *)s_stack_start,
         s_stack_size, (void *)code_start, (size_t)(code_end - code_start),
         s_ram_region, s_stack_region,
         s_code_region_needed ? "new:" : "existing:",
         s_code_region_needed ? s_code_region : 0U);
  return true;
}
