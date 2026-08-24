/* SPDX-License-Identifier: Apache-2.0 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/sys/crc.h>
#include <zephyr/sys/printk.h>

#include "pbl/services/filesystem/pfs.h"
#include "pfs_flash_shim.h"

#define TEST_FILE_NAME "zephyr-pfs-selftest"
#define TEST_PAYLOAD_SIZE 257u

extern void pfs_reset_all_state(void);

static int prv_fail(const char *detail, int result) {
  printk("PFS_FAIL %s %d\n", detail, result);
  return result == 0 ? -1 : result;
}

static void prv_fill_payload(uint8_t *payload, size_t size) {
  uint8_t value = 0x5au;

  for (size_t index = 0; index < size; ++index) {
    value = (uint8_t)((value * 33u) ^ (uint8_t)index ^ 0xa7u);
    payload[index] = value;
  }
}

static int prv_read_and_verify(const uint8_t *expected, uint8_t *actual,
                               size_t size, uint32_t *crc) {
  int fd = pfs_open(TEST_FILE_NAME, OP_FLAG_READ, FILE_TYPE_STATIC, 0);
  if (fd < 0) {
    return prv_fail("open_read", fd);
  }

  size_t file_size = pfs_get_file_size(fd);
  if (file_size != size) {
    (void)pfs_close(fd);
    return prv_fail("file_size", (int)file_size);
  }

  int bytes_read = pfs_read(fd, actual, size);
  if (bytes_read != (int)size) {
    (void)pfs_close(fd);
    return prv_fail("read", bytes_read);
  }

  if (memcmp(expected, actual, size) != 0) {
    (void)pfs_close(fd);
    return prv_fail("memcmp", -1);
  }

  *crc = crc32_ieee(actual, size);
  int result = pfs_close(fd);
  if (result < 0) {
    return prv_fail("close_read", result);
  }

  return 0;
}

int main(void) {
  uint8_t payload[TEST_PAYLOAD_SIZE];
  uint8_t readback[TEST_PAYLOAD_SIZE];
  uint32_t crc;
  int result;

  prv_fill_payload(payload, sizeof(payload));

  result = pfs_flash_shim_init();
  if (result != 0) {
    return prv_fail("flash_init", result);
  }

  result = pfs_init(false);
  if (result < 0) {
    return prv_fail("mount", result);
  }
  pfs_format(true);
  if (!pfs_active()) {
    return prv_fail("format", -1);
  }
  printk("PFS_MOUNT_OK\n");

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
  printk("PFS_WRITE_OK %d\n", bytes_written);

  memset(readback, 0, sizeof(readback));
  result = prv_read_and_verify(payload, readback, sizeof(readback), &crc);
  if (result != 0) {
    return result;
  }
  printk("PFS_READ_OK %08x\n", crc);

  pfs_reset_all_state();
  result = pfs_init(false);
  if (result < 0) {
    return prv_fail("remount", result);
  }

  memset(readback, 0, sizeof(readback));
  result = prv_read_and_verify(payload, readback, sizeof(readback), &crc);
  if (result != 0) {
    return result;
  }
  printk("PFS_PERSIST_OK\n");

  result = pfs_remove(TEST_FILE_NAME);
  if (result < 0) {
    return prv_fail("remove", result);
  }

  pfs_reset_all_state();
  result = pfs_init(false);
  if (result < 0) {
    return prv_fail("delete_remount", result);
  }

  fd = pfs_open(TEST_FILE_NAME, OP_FLAG_READ, FILE_TYPE_STATIC, 0);
  if (fd >= 0) {
    (void)pfs_close(fd);
    return prv_fail("delete_persist", fd);
  }

  printk("PFS_DONE\n");
  return 0;
}
