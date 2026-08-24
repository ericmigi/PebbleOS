/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

void pfs_port_panic(const char *file, int line, const char *format, ...)
    __attribute__((noreturn, format(printf, 3, 4)));

#define PBL_ASSERT(expr, format, ...)                    \
  do {                                                   \
    if (!(expr)) {                                       \
      pfs_port_panic(__FILE__, __LINE__, format,         \
                     ##__VA_ARGS__);                     \
    }                                                    \
  } while (0)

#define PBL_ASSERTN(expr)                                \
  do {                                                   \
    if (!(expr)) {                                       \
      pfs_port_panic(__FILE__, __LINE__, "assert: %s", \
                     #expr);                             \
    }                                                    \
  } while (0)

#define PBL_CROAK(format, ...) \
  pfs_port_panic(__FILE__, __LINE__, format, ##__VA_ARGS__)
