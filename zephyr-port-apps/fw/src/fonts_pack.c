/* SPDX-License-Identifier: Apache-2.0 */

// System-font lookup backed by the real resource pack: resolves any key in
// the reference build's generated font table and caches the loaded FontInfo.

#include <string.h>

#include "applib/fonts/fonts.h"
#include "applib/fonts/fonts_private.h"
#include "applib/graphics/text_resources.h"
#include "resource/resource_ids.auto.h"
#include "pbl/util/size.h"

#include "font_resource_keys.auto.h"

#include "font_resource_table.auto.h"

#define FW_FONT_CACHE_SIZE 8

typedef struct {
  const char *key;  // key_name pointer from the table (stable)
  FontInfo info;
} CachedFont;

static CachedFont s_cache[FW_FONT_CACHE_SIZE];
static size_t s_cache_used;

GFont fw_fonts_lookup_pack(const char *font_key) {
  for (size_t i = 0; i < s_cache_used; ++i) {
    if (!strcmp(s_cache[i].key, font_key)) {
      return &s_cache[i].info;
    }
  }
  for (size_t i = 0; i < ARRAY_LENGTH(s_font_resource_keys); ++i) {
    if (strcmp(s_font_resource_keys[i].key_name, font_key)) {
      continue;
    }
    if (s_cache_used >= FW_FONT_CACHE_SIZE) {
      return NULL;
    }
    CachedFont *slot = &s_cache[s_cache_used];
    if (!text_resources_init_font(SYSTEM_APP, s_font_resource_keys[i].resource_id,
                                  s_font_resource_keys[i].extension_id, &slot->info)) {
      return NULL;
    }
    slot->key = s_font_resource_keys[i].key_name;
    ++s_cache_used;
    return &slot->info;
  }
  return NULL;
}
