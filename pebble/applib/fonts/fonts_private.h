/* SPDX-FileCopyrightText: 2024 Google LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <pbl/kernel/compiler.h>

#include <resource/resource.h>

//
// Definitions only for font loading and text rendering
//

// Initial version
#define FONT_VERSION_1 1
// 4 byte codepoints in offset table
#define FONT_VERSION_2 2
// feature bits: 2 or 4 byte offsets, RLE encoding
#define FONT_VERSION_3    3
#define FEATURE_OFFSET_16 (1 << 0)
#define FEATURE_RLE4      (1 << 1)
// glyph bodies are color glyphs, see docs/reference/formats/font.md
#define FEATURE_COLOR (1 << 2)
// glyph bitmaps, offsets and advances are at twice the font's resolution, for displays scaled by 2
#define FEATURE_SCALE2 (1 << 3)
// glyph bitmaps hold 2 bits of coverage per pixel, 16 pixels per 32-bit word
#define FEATURE_AA2 (1 << 4)
// each glyph is a 1-bit bitmap followed by its edges at panel resolution: a uint16 count, then
// x, y and the coverage of the pixel's 2x2 panel pixels, 3 bytes each
#define FEATURE_EDGES (1 << 5)
// Edge records store a row-major gap followed by four coverage nibbles.
#define FEATURE_EDGES4 (1 << 6)

// HACK ALERT: Store the v3 FontMetaDataV3 feature bits in the top six bits of FontMetaData
// version field. We need this information at the lowest levels and can't extend FontMetaData
// for legacy support reasons.
// The unused internal version 0 denotes a v3 header with four-bit edges.
// Keep FontInfo within the existing app allocator ABI.
#define HAS_EDGES4(version)             (((version) & 0x03) == 0)
#define FONT_VERSION(version)           (HAS_EDGES4(version) ? FONT_VERSION_3 : ((version) & 0x03))
#define HAS_FEATURE(_version, _feature) ((_version) & (_feature))
#define VERSION_FIELD_FEATURE_OFFSET_16 (1 << 7)
#define VERSION_FIELD_FEATURE_RLE4      (1 << 6)
#define VERSION_FIELD_FEATURE_COLOR     (1 << 5)
#define VERSION_FIELD_FEATURE_SCALE2    (1 << 4)
#define VERSION_FIELD_FEATURE_AA2       (1 << 3)
#define VERSION_FIELD_FEATURE_EDGES     (1 << 2)

// There are now three versions of the FontMetaData structure: V1 (formerly known as 'legacy'), V2
// (still known as FontMetaData), and V3 (know as V3). We can't change the stack/memory usage
// until we drop support for existing applications so we can't simply use V3 as the base.
//
// The name 'FontMetaData' is retained instead of a more consistent 'FontMetaDataV2' because the
// uses of V1 and V3 are localized but 'FontMetaData' is used in many places, requiring many ugly
// changes.
typedef struct PBL_PACKED {
  uint8_t version;
  uint8_t max_height;
  uint16_t number_of_glyphs;
  uint16_t wildcard_codepoint;
  uint8_t hash_table_size;
  uint8_t codepoint_bytes;
  uint8_t size;
  uint8_t features;
} FontMetaDataV3;

typedef struct PBL_PACKED {
  uint8_t version;
  uint8_t max_height;
  uint16_t number_of_glyphs;
  uint16_t wildcard_codepoint;
  uint8_t hash_table_size;
  uint8_t codepoint_bytes;
} FontMetaData;

typedef struct PBL_PACKED {
  uint8_t version;
  uint8_t max_height;
  uint16_t number_of_glyphs;
  uint16_t wildcard_codepoint;
} FontMetaDataV1;

typedef struct {
  FontMetaData md;
  ResAppNum app_num;
  uint32_t resource_id;
} FontResource;

typedef struct {
  bool loaded;
  bool extended;
  uint8_t max_height;
  FontResource base;
  FontResource extension;
  ResourceCallbackHandle extension_changed_cb;
} FontInfo;

typedef struct PBL_PACKED {
  uint8_t hash;
  uint8_t count;
  uint16_t offset;
} FontHashTableEntry;
