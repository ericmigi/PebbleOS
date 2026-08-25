/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>

#include <zephyr/sys/printk.h>

#define LOG_DOMAIN_TEXT 0
#define LOG_DOMAIN_BT_STACK 1
#define LOG_LEVEL_ALWAYS 0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARNING 2
#define LOG_LEVEL_INFO 3
#define LOG_LEVEL_DEBUG 200
#define PBL_LOG_MODULE_DEFINE(name, level)
#define PBL_LOG_MODULE_DECLARE(name, level)
#define PBL_SHOULD_LOG(level) ((level) <= LOG_LEVEL_WARNING)

static inline void pbl_log_vargs(int level, const char *file, int line,
                                 const char *format, va_list args) {
  (void)level;
  (void)file;
  (void)line;
  vprintk(format, args);
  printk("\n");
}

#define PBL_LOG_D_DBG(domain, fmt, ...)
#define PBL_LOG_D_INFO(domain, fmt, ...)
#define PBL_LOG_D_WRN(domain, fmt, ...)
#define PBL_LOG_D_ERR(domain, fmt, ...) printk(fmt "\n", ##__VA_ARGS__)
#define PBL_LOG_DBG(...)
#define PBL_LOG_INFO(...)
#define PBL_LOG_WRN(...)
#define PBL_LOG_ERR(fmt, ...) printk(fmt "\n", ##__VA_ARGS__)
#define PBL_LOG_ALWAYS(fmt, ...) printk(fmt "\n", ##__VA_ARGS__)
