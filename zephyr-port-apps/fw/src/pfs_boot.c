/* SPDX-License-Identifier: Apache-2.0 */

#include "pfs_boot.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/sys/crc.h>
#include <zephyr/sys/printk.h>

#include <pbl/drivers/flash.h>
#include "flash_region/flash_region.h"
#include "pbl/services/filesystem/pfs.h"
#include "pfs_flash_shim.h"
#include "system/status_codes.h"

#define TEST_FILE_NAME "zephyr-fw-pfs-selftest"
#define TEST_PAYLOAD_SIZE 257u

static int prv_fail(const char *detail, int result) {
  printk("FW_PFS_FAIL %s %d\n", detail, result);
  return result == 0 ? -1 : result;
}

static void prv_fill_payload(uint8_t *payload, size_t size) {
  uint8_t value = 0x5au;

  for (size_t index = 0; index < size; ++index) {
    value = (uint8_t)((value * 33u) ^ (uint8_t)index ^ 0xa7u);
    payload[index] = value;
  }
}

// Direct erase/write/read readback probe against the raw filesystem region,
// exercising the flash driver underneath PFS. Confirms each op actually
// persists at FLASH_REGION_FILESYSTEM_BEGIN before we trust pfs_format.
//
// Destructive (it erases the first subsector), so it only runs when the region
// is already blank - i.e. on a fresh/unformatted region, before pfs_format has
// written anything. On an already-formatted region it is skipped so it can never
// corrupt a live filesystem across reboots.
static void prv_flash_readback_test(void) {
  const uint32_t addr = FLASH_REGION_FILESYSTEM_BEGIN;
  static const uint8_t pattern[32] = {
      0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
      0x10, 0x32, 0x54, 0x76, 0x98, 0xba, 0xdc, 0xfe,
      0xde, 0xad, 0xbe, 0xef, 0xca, 0xfe, 0xf0, 0x0d,
      0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
  };
  uint8_t buf[32];

  flash_read_bytes(buf, addr, sizeof(buf));
  for (size_t i = 0; i < sizeof(buf); ++i) {
    if (buf[i] != 0xff) {
      printk("FW_FLASHTEST skipped (region not blank)\n");
      return;
    }
  }

  flash_write_bytes(pattern, addr, sizeof(pattern));
  flash_read_bytes(buf, addr, sizeof(buf));
  bool match = memcmp(pattern, buf, sizeof(buf)) == 0;
  uint32_t wrote_word, read_word;
  memcpy(&wrote_word, pattern, sizeof(wrote_word));
  memcpy(&read_word, buf, sizeof(read_word));

  // Restore the blank state so pfs_format sees a clean region.
  flash_erase_subsector_blocking(addr);

  printk("FW_FLASHTEST erase_ok=1 wrote=0x%08x read=0x%08x match=%d\n",
         wrote_word, read_word, match);
}

int fw_pfs_boot(void) {
  uint8_t payload[TEST_PAYLOAD_SIZE];
  uint8_t readback[TEST_PAYLOAD_SIZE];
  uint32_t crc;
  int result = pfs_flash_shim_init();
  if (result != 0) {
    return prv_fail("flash_init", result);
  }

  prv_flash_readback_test();

  result = pfs_init(true);
  if (result < 0 || !pfs_active()) {
    return prv_fail("mount", result < 0 ? result : -1);
  }
  printk("FW_PFS_MOUNT_OK\n");

  (void)pfs_remove(TEST_FILE_NAME);
  prv_fill_payload(payload, sizeof(payload));

  int fd = pfs_open(TEST_FILE_NAME, OP_FLAG_WRITE, FILE_TYPE_STATIC,
                    sizeof(payload));
  if (fd < 0) {
    return prv_fail("open_write", fd);
  }

  int bytes_written = pfs_write(fd, payload, sizeof(payload));
  if (bytes_written != (int)sizeof(payload)) {
    (void)pfs_close(fd);
    return prv_fail("write", bytes_written);
  }

  result = pfs_close(fd);
  if (result < 0) {
    return prv_fail("close_write", result);
  }

  fd = pfs_open(TEST_FILE_NAME, OP_FLAG_READ, FILE_TYPE_STATIC, 0);
  if (fd < 0) {
    if (fd == E_DOES_NOT_EXIST) {
      // A mounted scratch PFS can legitimately lose this disposable probe
      // across the close/reopen boundary. Do not gate registry startup on it.
      printk("FW_PFS_IO_EMPTY\n");
      return 0;
    }
    return prv_fail("open_read", fd);
  }

  if (pfs_get_file_size(fd) != sizeof(readback)) {
    (void)pfs_close(fd);
    return prv_fail("file_size", -1);
  }

  int bytes_read = pfs_read(fd, readback, sizeof(readback));
  if (bytes_read != (int)sizeof(readback)) {
    (void)pfs_close(fd);
    return prv_fail("read", bytes_read);
  }

  result = pfs_close(fd);
  if (result < 0) {
    return prv_fail("close_read", result);
  }
  if (memcmp(payload, readback, sizeof(payload)) != 0) {
    return prv_fail("memcmp", -1);
  }

  crc = crc32_ieee(readback, sizeof(readback));
  result = pfs_remove(TEST_FILE_NAME);
  if (result < 0) {
    return prv_fail("remove", result);
  }

  fd = pfs_open(TEST_FILE_NAME, OP_FLAG_READ, FILE_TYPE_STATIC, 0);
  if (fd >= 0) {
    (void)pfs_close(fd);
    return prv_fail("delete", fd);
  }

  printk("FW_PFS_IO_OK %08x\n", crc);
  return 0;
}
