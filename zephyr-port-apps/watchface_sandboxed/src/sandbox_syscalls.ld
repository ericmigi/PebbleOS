#include <zephyr/linker/sections.h>

SECTION_PROLOGUE(sandbox_syscall_text,,)
{
  __sandbox_syscall_start = .;
  KEEP(*(.sandbox_syscall_text.*))
  __sandbox_syscall_end = .;
} GROUP_LINK_IN(ROMABLE_REGION)
