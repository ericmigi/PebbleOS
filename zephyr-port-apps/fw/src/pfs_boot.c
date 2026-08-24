/* SPDX-License-Identifier: Apache-2.0 */

#include "pfs_boot.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/sys/crc.h>
#include <zephyr/sys/printk.h>

#include "pbl/services/filesystem/pfs.h"
#include "pfs_flash_shim.h"

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

int fw_pfs_boot(void) {
  uint8_t payload[TEST_PAYLOAD_SIZE];
  uint8_t readback[TEST_PAYLOAD_SIZE];
  uint32_t crc;
  int result = pfs_flash_shim_init();
  if (result != 0) {
    return prv_fail("flash_init", result);
  }

  result = pfs_init(false);
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
