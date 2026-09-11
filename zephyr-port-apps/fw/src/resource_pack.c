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
#include <zephyr/sys/printk.h>

#include "applib/applib_resource.h"
#include "flash_region/flash_region.h"
#include "resource/resource.h"
#include "pbl/services/filesystem/pfs.h"

void *applib_malloc(size_t size);
void applib_free(void *ptr);

void fw_pbw_file_name(char *buf, size_t buf_len, int32_t id, const char *suffix);

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

// Builtin resources (action bar icons etc.) are compiled into the firmware
// in shipping; reuse the reference build's generated table.
typedef struct BuiltInResourceData {
  uint32_t resource_id;
  const uint8_t *contents;
  const uint32_t num_bytes;
} BuiltInResourceData;
extern const BuiltInResourceData g_builtin_resources[];
extern const uint32_t g_num_builtin_resources;

static const uint8_t *prv_builtin_resource(uint32_t resource_id, size_t *size_out) {
  for (uint32_t i = 0; i < g_num_builtin_resources; ++i) {
    if (g_builtin_resources[i].resource_id == resource_id) {
      if (size_out) {
        *size_out = g_builtin_resources[i].num_bytes;
      }
      return g_builtin_resources[i].contents;
    }
  }
  return NULL;
}

// Phone-installed app packs live in PFS ("@<id>/res", pbpack layout: 12-byte
// manifest, 256 x 16-byte table, content). Not mappable: callers get copies.
#define APP_PACK_MAX_ENTRIES 256U
#define APP_PACK_CONTENT_OFFSET (PACK_MANIFEST_SIZE + PACK_TABLE_ENTRY_SIZE * APP_PACK_MAX_ENTRIES)

static int prv_app_pack_open(ResAppNum app_num) {
  char name[32];
  fw_pbw_file_name(name, sizeof(name), (int32_t)app_num, "res");
  return pfs_open(name, OP_FLAG_READ, FILE_TYPE_STATIC, 0);
}

// Content offset + size of resource_id in app_num's pack, or false.
static bool prv_app_pack_locate(ResAppNum app_num, uint32_t resource_id, uint32_t *offset_out,
                                size_t *size_out) {
  if (resource_id == 0U || resource_id > APP_PACK_MAX_ENTRIES) {
    return false;
  }
  const int fd = prv_app_pack_open(app_num);
  if (fd < 0) {
    return false;
  }
  uint8_t hdr[4], entry[PACK_TABLE_ENTRY_SIZE];
  bool ok = pfs_read(fd, hdr, sizeof(hdr)) == (int)sizeof(hdr) &&
            resource_id <= prv_read_u32(hdr) &&
            pfs_seek(fd, PACK_MANIFEST_SIZE + (resource_id - 1U) * PACK_TABLE_ENTRY_SIZE, FSeekSet) >= 0 &&
            pfs_read(fd, entry, sizeof(entry)) == (int)sizeof(entry) &&
            prv_read_u32(entry) == resource_id;
  pfs_close(fd);
  if (!ok) {
    return false;
  }
  *offset_out = APP_PACK_CONTENT_OFFSET + prv_read_u32(entry + 4U);
  *size_out = prv_read_u32(entry + 8U);
  return true;
}

static size_t prv_app_pack_read(ResAppNum app_num, uint32_t offset, uint8_t *buf, size_t n) {
  const int fd = prv_app_pack_open(app_num);
  if (fd < 0) {
    return 0;
  }
  int got = 0;
  if (pfs_seek(fd, (int)offset, FSeekSet) >= 0) {
    got = pfs_read(fd, buf, n);
  }
  pfs_close(fd);
  return got > 0 ? (size_t)got : 0;
}

static const uint8_t *prv_lookup(ResAppNum app_num, uint32_t resource_id, size_t *size_out) {
  if (app_num != SYSTEM_APP) {
    return NULL;
  }
  // port.c's private IDs (embedded fonts/icons + the sliding-text pack) win;
  // everything else comes from the real system pack, then the builtin table.
  size_t port_size = 0;
  const uint8_t *port_data = port_sys_resource_read_only_bytes(app_num, resource_id, &port_size);
  if (port_data != NULL) {
    if (size_out) {
      *size_out = port_size;
    }
    return port_data;
  }
  const uint8_t *pack = prv_pack_resource(resource_id, size_out);
  if (pack != NULL) {
    return pack;
  }
  return prv_builtin_resource(resource_id, size_out);
}

bool sys_resource_is_valid(ResAppNum app_num, uint32_t resource_id) {
  if (app_num != SYSTEM_APP) {
    uint32_t off; size_t size;
    return prv_app_pack_locate(app_num, resource_id, &off, &size);
  }
  return prv_lookup(app_num, resource_id, NULL) != NULL;
}

size_t sys_resource_size(ResAppNum app_num, uint32_t resource_id) {
  if (app_num != SYSTEM_APP) {
    uint32_t off; size_t size;
    return prv_app_pack_locate(app_num, resource_id, &off, &size) ? size : 0;
  }
  size_t size = 0;
  (void)prv_lookup(app_num, resource_id, &size);
  return size;
}

size_t sys_resource_load_range(ResAppNum app_num, uint32_t resource_id, uint32_t start_bytes,
                               uint8_t *buffer, size_t num_bytes) {
  if (app_num != SYSTEM_APP) {
    uint32_t off; size_t size;
    if (!prv_app_pack_locate(app_num, resource_id, &off, &size) || start_bytes >= size) {
      return 0;
    }
    const size_t n = (num_bytes < size - start_bytes) ? num_bytes : (size - start_bytes);
    return prv_app_pack_read(app_num, off + start_bytes, buffer, n);
  }
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
  if (app_num != SYSTEM_APP) {
    return NULL;  // not mappable; callers fall back to load
  }
  return prv_lookup(app_num, resource_id, num_bytes_out);
}

bool sys_resource_bytes_are_readonly(void *bytes) {
  // Only heap copies live in SRAM; the XIP pack, the builtin table in the code
  // image (0x08.. on qemu, 0x12.. on the SF32) and port.c's embedded data are
  // all flash. Writing to any of them stalls the SF32's AHB with no fault.
  const uintptr_t address = (uintptr_t)bytes;
  if (address < 0x20000000u || address >= 0x30000000u) {
    return true;
  }
  return port_sys_resource_bytes_are_readonly(bytes);
}

void *applib_resource_mmap_or_load(ResAppNum app_num, uint32_t resource_id, size_t offset,
                                   size_t length, bool use_aligned) {
  (void)use_aligned;
  if (app_num != SYSTEM_APP) {
    uint32_t off; size_t size;
    if (!prv_app_pack_locate(app_num, resource_id, &off, &size) || offset > size ||
        length > size - offset) {
      return NULL;
    }
    uint8_t *copy = applib_malloc(length);  // freed by applib_resource_munmap_or_free
    if (copy && prv_app_pack_read(app_num, off + offset, copy, length) != length) {
      applib_free(copy);
      copy = NULL;
    }
    return copy;
  }
  size_t size = 0;
  const uint8_t *data = prv_lookup(app_num, resource_id, &size);
  if (!data || offset > size || length > size - offset) {
    return NULL;
  }
  return (void *)(data + offset);
}

void applib_resource_munmap_or_free(void *bytes) {
  if (bytes && !sys_resource_bytes_are_readonly(bytes)) {
    applib_free(bytes);  // loaded copy (app pack, or gbitmap's decoded pixels)
    return;
  }
  port_applib_resource_munmap_or_free(bytes);
}

// XIP-served resources (system pack, builtin table in the code image) are
// read-only; gbitmap must not memmove them in place.
bool applib_resource_is_mmapped(const void *bytes) {
  return sys_resource_bytes_are_readonly((void *)bytes);
}

ResHandle applib_resource_get_handle(uint32_t resource_id) {
  ResAppNum sys_get_current_resource_num(void);
  return sys_resource_is_valid(sys_get_current_resource_num(), resource_id)
             ? (ResHandle)(uintptr_t)resource_id : NULL;
}

// resource.h entry used by the real timezone-database service (compiled on the
// qemu shell); same lookup as sys_resource_load_range.
size_t resource_load_byte_range_system(ResAppNum app_num, uint32_t resource_id,
                                       uint32_t start_offset, uint8_t *data, size_t num_bytes) {
  return sys_resource_load_range(app_num, resource_id, start_offset, data, num_bytes);
}

ResAppNum sys_get_current_resource_num(void);

// applib_resource.c entries the SDK table exports (fonts_load_custom_font
// sizes/loads through these): resolve against the running app's bank.
size_t applib_resource_size(ResHandle h) {
  return sys_resource_size(sys_get_current_resource_num(), (uint32_t)(uintptr_t)h);
}

size_t applib_resource_load(ResHandle h, uint8_t *buffer, size_t max_length) {
  return sys_resource_load_range(sys_get_current_resource_num(), (uint32_t)(uintptr_t)h, 0, buffer,
                                 max_length);
}

size_t applib_resource_load_byte_range(ResHandle h, uint32_t start_offset, uint8_t *buffer,
                                       size_t num_bytes) {
  return sys_resource_load_range(sys_get_current_resource_num(), (uint32_t)(uintptr_t)h,
                                 start_offset, buffer, num_bytes);
}
