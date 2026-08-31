/* SPDX-License-Identifier: Apache-2.0 */

// qemu_emery: serve the REAL system resource pack (same pbpack the FreeRTOS
// reference uses) straight from the XIP-mapped external flash, so the real
// launcher/glance PDC icons render pixel-identical. port.c's embedded resources
// (fonts, music icons, FW_RES_*) keep priority for their private IDs; its
// sys_resource_*/applib_resource_* definitions are renamed to port_* for this
// build (see CMakeLists) and used as the fallback.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "applib/applib_resource.h"
#include "flash_region/flash_region.h"
#include "resource/resource.h"

#define PACK_BASE ((const uint8_t *)FLASH_REGION_SYSTEM_RESOURCES_BANK_0_BEGIN)
#define PACK_MAX_SIZE \
  (FLASH_REGION_SYSTEM_RESOURCES_BANK_0_END - FLASH_REGION_SYSTEM_RESOURCES_BANK_0_BEGIN)
#define PACK_MANIFEST_SIZE 12U
#define PACK_TABLE_ENTRY_SIZE 16U
#define PACK_MAX_ENTRIES 768U
#define PACK_CONTENT_OFFSET (PACK_MANIFEST_SIZE + PACK_TABLE_ENTRY_SIZE * PACK_MAX_ENTRIES)

// port.c's originals, renamed via compile definitions on that TU.
bool port_sys_resource_is_valid(ResAppNum app_num, uint32_t resource_id);
size_t port_sys_resource_size(ResAppNum app_num, uint32_t resource_id);
size_t port_sys_resource_load_range(ResAppNum app_num, uint32_t resource_id, uint32_t start_bytes,
                                    uint8_t *buffer, size_t num_bytes);
uint32_t port_sys_resource_get_and_cache(ResAppNum app_num, uint32_t resource_id);
const uint8_t *port_sys_resource_read_only_bytes(ResAppNum app_num, uint32_t resource_id,
                                                 size_t *num_bytes_out);
bool port_sys_resource_bytes_are_readonly(void *bytes);
void *port_applib_resource_mmap_or_load(ResAppNum app_num, uint32_t resource_id, size_t offset,
                                        size_t length, bool use_aligned);
void port_applib_resource_munmap_or_free(void *bytes);

static uint32_t prv_read_u32(const uint8_t *bytes) {
  uint32_t value;
  memcpy(&value, bytes, sizeof(value));
  return value;
}

static const uint8_t *prv_pack_resource(uint32_t resource_id, size_t *size_out) {
  const uint32_t num_resources = prv_read_u32(PACK_BASE);
  if (resource_id == 0U || resource_id > num_resources || num_resources > PACK_MAX_ENTRIES) {
    return NULL;
  }
  const uint8_t *entry = PACK_BASE + PACK_MANIFEST_SIZE +
                         (resource_id - 1U) * PACK_TABLE_ENTRY_SIZE;
  const uint32_t entry_id = prv_read_u32(entry);
  const uint32_t offset = prv_read_u32(entry + 4U);
  const uint32_t length = prv_read_u32(entry + 8U);
  if (entry_id != resource_id || offset > PACK_MAX_SIZE - PACK_CONTENT_OFFSET ||
      length > PACK_MAX_SIZE - PACK_CONTENT_OFFSET - offset) {
    return NULL;
  }
  if (size_out) {
    *size_out = length;
  }
  return PACK_BASE + PACK_CONTENT_OFFSET + offset;
}

static const uint8_t *prv_lookup(ResAppNum app_num, uint32_t resource_id, size_t *size_out) {
  if (app_num != SYSTEM_APP) {
    return NULL;
  }
  // port.c's private IDs (embedded fonts/icons + the sliding-text pack) win;
  // everything else comes from the real system pack.
  size_t port_size = 0;
  const uint8_t *port_data = port_sys_resource_read_only_bytes(app_num, resource_id, &port_size);
  if (port_data != NULL) {
    if (size_out) {
      *size_out = port_size;
    }
    return port_data;
  }
  return prv_pack_resource(resource_id, size_out);
}

bool sys_resource_is_valid(ResAppNum app_num, uint32_t resource_id) {
  return prv_lookup(app_num, resource_id, NULL) != NULL;
}

size_t sys_resource_size(ResAppNum app_num, uint32_t resource_id) {
  size_t size = 0;
  (void)prv_lookup(app_num, resource_id, &size);
  return size;
}

size_t sys_resource_load_range(ResAppNum app_num, uint32_t resource_id, uint32_t start_bytes,
                               uint8_t *buffer, size_t num_bytes) {
  size_t size = 0;
  const uint8_t *data = prv_lookup(app_num, resource_id, &size);
  if (!data || start_bytes >= size) {
    return 0;
  }
  const size_t copy_size = (num_bytes < size - start_bytes) ? num_bytes : (size - start_bytes);
  memcpy(buffer, data + start_bytes, copy_size);
  return copy_size;
}

uint32_t sys_resource_get_and_cache(ResAppNum app_num, uint32_t resource_id) {
  return sys_resource_is_valid(app_num, resource_id) ? resource_id : 0;
}

const uint8_t *sys_resource_read_only_bytes(ResAppNum app_num, uint32_t resource_id,
                                            size_t *num_bytes_out) {
  return prv_lookup(app_num, resource_id, num_bytes_out);
}

bool sys_resource_bytes_are_readonly(void *bytes) {
  const uintptr_t address = (uintptr_t)bytes;
  if (address >= (uintptr_t)PACK_BASE && address < (uintptr_t)(PACK_BASE + PACK_MAX_SIZE)) {
    return true;
  }
  return port_sys_resource_bytes_are_readonly(bytes);
}

void *applib_resource_mmap_or_load(ResAppNum app_num, uint32_t resource_id, size_t offset,
                                   size_t length, bool use_aligned) {
  (void)use_aligned;
  size_t size = 0;
  const uint8_t *data = prv_lookup(app_num, resource_id, &size);
  if (!data || offset > size || length > size - offset) {
    return NULL;
  }
  return (void *)(data + offset);
}

void applib_resource_munmap_or_free(void *bytes) {
  port_applib_resource_munmap_or_free(bytes);
}

ResHandle applib_resource_get_handle(uint32_t resource_id) {
  return sys_resource_is_valid(SYSTEM_APP, resource_id) ? (ResHandle)(uintptr_t)resource_id : NULL;
}
