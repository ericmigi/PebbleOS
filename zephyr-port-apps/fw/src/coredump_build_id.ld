/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/linker/sections.h>

SECTION_PROLOGUE(.note.gnu.build-id,,)
{
  . = ALIGN(4);
  TINTIN_BUILD_ID = .;
  KEEP(*(.note.gnu.build-id))
} GROUP_LINK_IN(ROMABLE_REGION)
