/* SPDX-License-Identifier: Apache-2.0 */

#include <stddef.h>

#include <zephyr/kernel.h>

// mbedtls allocates via kernel_calloc / kernel_free (see pebble_mbedtls_config.h).
// The folded-in notification render path (notif/src/port.c) also provides the
// kernel_* / task_* / applib_* allocators, all backed by the Zephyr k_heap, and
// it defines kernel_free. Keep only kernel_calloc here, k_heap-backed too, so an
// mbedtls object allocated here and freed through port.c's kernel_free stays on
// one heap.
void *kernel_calloc(size_t count, size_t size) {
  return k_calloc(count, size);
}
