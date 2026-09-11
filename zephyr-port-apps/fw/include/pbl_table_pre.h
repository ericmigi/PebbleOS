/* SPDX-License-Identifier: Apache-2.0 */
// Force-included into the generated pebble.auto.c: prototypes the minimal
// libc headers lack for entries the exported table takes the address of.
#pragma once
#include <stddef.h>
long atol(const char *s);
int atoi(const char *s);
char *strncat(char *dst, const char *src, size_t n);
