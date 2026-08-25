/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stdarg.h>

#include <zephyr/sys/printk.h>

#define LOG_LEVEL_DEBUG 4
#define LOG_LEVEL_INFO 3
#define LOG_LEVEL_WARNING 2
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_ALWAYS 0
#define LOG_DOMAIN_BT_STACK 1

#define PBL_LOG_MODULE_DECLARE(...)
#define PBL_LOG_MODULE_DEFINE(...)
// Only WARNING(2)/ERROR(1)/ALWAYS(0). Gating out nimble's DEBUG/INFO MODLOG
// (per-ACL byte dumps) keeps the 1Mbaud console from stalling the BLE task,
// which was timing out the phone's PPoGATT CCCD subscribe.
#define PBL_SHOULD_LOG(level) ((level) <= LOG_LEVEL_WARNING)

static inline void pbl_log_vargs(int level, const char *file, int line,
                                 const char *fmt, va_list args) {
  (void)level;
  (void)file;
  (void)line;
  vprintk(fmt, args);
  printk("\n");
}

#define PBL_LOG(level, fmt, ...) printk(fmt "\n", ##__VA_ARGS__)
#define PBL_LOG_DBG(fmt, ...) printk(fmt "\n", ##__VA_ARGS__)
#define PBL_LOG_INFO(fmt, ...) printk(fmt "\n", ##__VA_ARGS__)
#define PBL_LOG_WRN(fmt, ...) printk(fmt "\n", ##__VA_ARGS__)
#define PBL_LOG_ERR(fmt, ...) printk(fmt "\n", ##__VA_ARGS__)
#define PBL_LOG_D_DBG(domain, fmt, ...) printk(fmt "\n", ##__VA_ARGS__)
#define PBL_LOG_D_INFO(domain, fmt, ...) printk(fmt "\n", ##__VA_ARGS__)
#define PBL_LOG_D_ERR(domain, fmt, ...) printk(fmt "\n", ##__VA_ARGS__)
