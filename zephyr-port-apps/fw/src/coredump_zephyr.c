/* SPDX-License-Identifier: Apache-2.0 */

#include "coredump_zephyr.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/arch/cpu.h>
#include <zephyr/arch/arm/nmi.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/fatal.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "console/dbgserial.h"
#include "flash_region/flash_region.h"
#include "kernel/core_dump.h"
#include "kernel/core_dump_private.h"
#include "pbl/util/build_id.h"
#include "system/bootbits.h"
#include "system/reboot_reason.h"
#include "system/reset.h"
#include "system/version.h"

const char *mfg_get_serial_number(void);
void flash_read_bytes(uint8_t *buffer, uint32_t address, uint32_t size);
void flash_write_bytes(const uint8_t *buffer, uint32_t address, uint32_t size);

#define COREDUMP_FLASH_NODE DT_NODELABEL(mpi2)
#define COREDUMP_UART_NODE DT_CHOSEN(zephyr_console)
#define FLASH_DEVICE_OFFSET(address) ((address) - FLASH_REGION_BASE_ADDRESS)
#define COREDUMP_UART_BYTES_PER_LINE 32u
#define COREDUMP_FLASH_BOUNCE_SIZE 256u

static const struct device *const s_flash = DEVICE_DT_GET(COREDUMP_FLASH_NODE);
static const struct device *const s_uart = DEVICE_DT_GET(COREDUMP_UART_NODE);
static bool s_flash_failed;
static bool s_fatal_entered;
static RebootReason s_reboot_reason;
static uint8_t s_flash_bounce[COREDUMP_FLASH_BOUNCE_SIZE] __aligned(4);

extern const ElfExternalNote TINTIN_BUILD_ID;
extern void NMI_Handler(void);

static int prv_install_nmi_writer(void) {
  z_arm_nmi_set_handler(NMI_Handler);
  return 0;
}

SYS_INIT(prv_install_nmi_writer, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

static void prv_uart_byte(uint8_t byte) {
  if (device_is_ready(s_uart)) {
    uart_poll_out(s_uart, byte);
  }
}

static void prv_uart_string(const char *string) {
  while (*string != '\0') {
    prv_uart_byte((uint8_t)*string++);
  }
}

void dbgserial_putchar(uint8_t byte) {
  prv_uart_byte(byte);
}

void dbgserial_putstr(const char *string) {
  prv_uart_string(string);
}

void dbgserial_flush(void) {
  // uart_poll_out() is synchronous.
}

char *itoa(int value, char *buffer, int base) {
  char reversed[33];
  unsigned int magnitude;
  size_t count = 0;
  bool negative = value < 0 && base == 10;

  if (negative) {
    magnitude = (unsigned int)(-(value + 1)) + 1u;
  } else {
    magnitude = (unsigned int)value;
  }
  do {
    unsigned int digit = magnitude % (unsigned int)base;
    reversed[count++] = (char)(digit < 10u ? '0' + digit : 'a' + digit - 10u);
    magnitude /= (unsigned int)base;
  } while (magnitude != 0u);
  if (negative) {
    reversed[count++] = '-';
  }
  for (size_t i = 0; i < count; ++i) {
    buffer[i] = reversed[count - i - 1u];
  }
  buffer[count] = '\0';
  return buffer;
}

static bool prv_coredump_range(uint32_t address, uint32_t size) {
  return address >= FLASH_REGION_CD_BEGIN && address <= FLASH_REGION_CD_END &&
         size <= FLASH_REGION_CD_END - address;
}

void cd_flash_init(void) {
  s_flash_failed = !device_is_ready(s_flash);
  if (s_flash_failed) {
    prv_uart_string("COREDUMP_FLASH_NOT_READY\n");
    system_hard_reset();
  }
}

void cd_flash_read_bytes(void *buffer, uint32_t address, uint32_t size) {
  if (!prv_coredump_range(address, size) ||
      flash_read(s_flash, FLASH_DEVICE_OFFSET(address), buffer, size) != 0) {
    memset(buffer, 0xff, size);
    s_flash_failed = true;
    prv_uart_string("COREDUMP_FLASH_READ_FAILED\n");
    system_hard_reset();
  }
}

uint32_t cd_flash_write_bytes(const void *buffer, uint32_t address, uint32_t size) {
  if (s_flash_failed || !prv_coredump_range(address, size)) {
    goto fail;
  }

  const uint8_t *source = buffer;
  uint32_t written = 0;
  while (written < size) {
    size_t chunk = MIN(sizeof(s_flash_bounce), size - written);
    memcpy(s_flash_bounce, source + written, chunk);
    if (flash_write(s_flash, FLASH_DEVICE_OFFSET(address + written),
                    s_flash_bounce, chunk) != 0) {
      goto fail;
    }
    written += chunk;
  }
  return size;

fail:
  s_flash_failed = true;
  prv_uart_string("COREDUMP_FLASH_WRITE_FAILED\n");
  system_hard_reset();
}

void cd_flash_erase_region(uint32_t address, uint32_t size) {
  if (s_flash_failed || !prv_coredump_range(address, size) || size == 0u ||
      (address & (SUBSECTOR_SIZE_BYTES - 1u)) != 0u ||
      (size & (SUBSECTOR_SIZE_BYTES - 1u)) != 0u) {
    goto fail;
  }

  while (size != 0u) {
    uint32_t device_address = FLASH_DEVICE_OFFSET(address);
    size_t chunk = SUBSECTOR_SIZE_BYTES;
    if ((device_address & (SECTOR_SIZE_BYTES - 1u)) == 0u &&
        size >= SECTOR_SIZE_BYTES) {
      chunk = SECTOR_SIZE_BYTES;
    }
    if (flash_erase(s_flash, device_address, chunk) != 0) {
      goto fail;
    }
    address += chunk;
    size -= chunk;
  }
  return;

fail:
  s_flash_failed = true;
  prv_uart_string("COREDUMP_FLASH_ERASE_FAILED\n");
  system_hard_reset();
}

const char *mfg_get_serial_number(void) {
  // ponytail: manufacturing-info storage is not mounted by the Zephyr port.
  return "ZEPHYR-PT2";
}

void version_copy_build_id_hex_string(char *buffer, size_t buffer_size,
                                      const ElfExternalNote *note) {
  static const char digits[] = "0123456789abcdef";
  const uint8_t *id = &note->data[note->name_length];
  size_t bytes = MIN(note->data_length, (buffer_size - 1u) / 2u);

  for (size_t i = 0; i < bytes; ++i) {
    buffer[i * 2u] = digits[id[i] >> 4];
    buffer[i * 2u + 1u] = digits[id[i] & 0xfu];
  }
  buffer[bytes * 2u] = '\0';
}

void boot_bit_set(BootBitValue bit) {
  ARG_UNUSED(bit);
}

void boot_bit_clear(BootBitValue bit) {
  ARG_UNUSED(bit);
}

bool boot_bit_test(BootBitValue bit) {
  ARG_UNUSED(bit);
  return false;
}

void watchdog_feed(void) {
  // ponytail: the unified Zephyr firmware does not arm the Pebble watchdog yet.
}

void reboot_reason_get(RebootReason *reason) {
  *reason = s_reboot_reason;
}

void reboot_reason_clear(void) {
  s_reboot_reason = (RebootReason){};
}

NORETURN system_hard_reset(void) {
  dbgserial_flush();
  NVIC_SystemReset();
  CODE_UNREACHABLE;
}

static bool prv_read(uint32_t address, void *buffer, size_t size) {
  return device_is_ready(s_flash) && prv_coredump_range(address, size) &&
         flash_read(s_flash, FLASH_DEVICE_OFFSET(address), buffer, size) == 0;
}

static void prv_emit_hex(const uint8_t *bytes, size_t size) {
  static const char digits[] = "0123456789abcdef";
  prv_uart_string("COREDUMP_DATA ");
  for (size_t i = 0; i < size; ++i) {
    prv_uart_byte(digits[bytes[i] >> 4]);
    prv_uart_byte(digits[bytes[i] & 0xfu]);
  }
  prv_uart_byte('\n');
}

bool coredump_zephyr_emit_uart(void) {
  const uint32_t slot = core_dump_get_slot_address(0);
  CoreDumpFlashRegionHeader region;
  CoreDumpImageHeader image;

  if (!prv_read(slot, &region, sizeof(region)) ||
      !prv_read(slot + sizeof(region), &image, sizeof(image)) ||
      region.magic != CORE_DUMP_FLASH_HDR_MAGIC || image.magic != CORE_DUMP_MAGIC) {
    return false;
  }
  if (region.unread == 0u) {
    return true;
  }

  uint32_t size;
  if (core_dump_size(slot, &size) != S_SUCCESS || size > CORE_DUMP_MAX_SIZE) {
    printk("COREDUMP_INVALID\n");
    return true;
  }

  char build_id[BUILD_ID_EXPECTED_LEN * 2u + 1u];
  version_copy_build_id_hex_string(build_id, sizeof(build_id), &TINTIN_BUILD_ID);
  printk("COREDUMP_BEGIN size=%u slot=%#x encoding=hex build=%s\n",
         size, slot, build_id);
  uint8_t buffer[COREDUMP_UART_BYTES_PER_LINE];
  uint32_t address = slot + sizeof(CoreDumpFlashRegionHeader);
  for (uint32_t offset = 0; offset < size; offset += sizeof(buffer)) {
    size_t chunk = MIN(sizeof(buffer), size - offset);
    if (!prv_read(address + offset, buffer, chunk)) {
      printk("COREDUMP_ABORT offset=%u\n", offset);
      return true;
    }
    prv_emit_hex(buffer, chunk);
  }
  printk("COREDUMP_END size=%u\n", size);
  core_dump_mark_read(slot);
  return true;
}

void coredump_zephyr_test_fault(void) {
#if defined(PEBBLE_COREDUMP_TEST_FAULT)
  printk("COREDUMP_TEST_FAULT\n");
  volatile uint32_t *invalid = (volatile uint32_t *)0xfffffffc;
  *invalid = 0xc0def00du;
#endif
}

FUNC_NORETURN void coredump_zephyr_capture_fatal(
    unsigned int reason, const struct arch_esf *esf) {
  if (s_fatal_entered) {
    prv_uart_string("COREDUMP_FATAL_REENTER\n");
    system_hard_reset();
  }
  s_fatal_entered = true;
  printk("COREDUMP_CAPTURE reason=%u\n", reason);

  CoreDumpSavedRegisters registers = {};
  if (esf != NULL) {
    registers.core_reg[portCANONICAL_REG_INDEX_R0] = esf->basic.r0;
    registers.core_reg[portCANONICAL_REG_INDEX_R1] = esf->basic.r1;
    registers.core_reg[portCANONICAL_REG_INDEX_R2] = esf->basic.r2;
    registers.core_reg[portCANONICAL_REG_INDEX_R3] = esf->basic.r3;
    registers.core_reg[portCANONICAL_REG_INDEX_R12] = esf->basic.ip;
    registers.core_reg[portCANONICAL_REG_INDEX_LR] = esf->basic.lr;
    registers.core_reg[portCANONICAL_REG_INDEX_PC] = esf->basic.pc;
    registers.core_reg[portCANONICAL_REG_INDEX_XPSR] = esf->basic.xpsr;
#if defined(CONFIG_EXTRA_EXCEPTION_INFO)
    const _callee_saved_t *callee = esf->extra_info.callee;
    if (callee != NULL) {
      registers.core_reg[portCANONICAL_REG_INDEX_R4] = callee->v1;
      registers.core_reg[portCANONICAL_REG_INDEX_R5] = callee->v2;
      registers.core_reg[portCANONICAL_REG_INDEX_R6] = callee->v3;
      registers.core_reg[portCANONICAL_REG_INDEX_R7] = callee->v4;
      registers.core_reg[portCANONICAL_REG_INDEX_R8] = callee->v5;
      registers.core_reg[portCANONICAL_REG_INDEX_R9] = callee->v6;
      registers.core_reg[portCANONICAL_REG_INDEX_R10] = callee->v7;
      registers.core_reg[portCANONICAL_REG_INDEX_R11] = callee->v8;
      registers.core_reg[portCANONICAL_REG_INDEX_SP] = callee->psp;
      registers.extra_reg.psp = callee->psp;
    }
    registers.extra_reg.msp = esf->extra_info.msp;
#endif
  }
  registers.extra_reg.primask = __get_PRIMASK();
  registers.extra_reg.basepri = __get_BASEPRI();
  registers.extra_reg.faultmask = __get_FAULTMASK();
  registers.extra_reg.control = __get_CONTROL();
  core_dump_set_fault_registers(&registers);
  core_dump_reset(false);
}
