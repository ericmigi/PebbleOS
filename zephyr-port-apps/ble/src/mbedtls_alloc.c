/* SPDX-License-Identifier: Apache-2.0 */

#include <stddef.h>
#include <stdlib.h>

void *kernel_calloc(size_t count, size_t size) {
  return calloc(count, size);
}

void kernel_free(void *ptr) {
  free(ptr);
}
