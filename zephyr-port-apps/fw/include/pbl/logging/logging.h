/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>

void printk(const char *format, ...);

#define LOG_DOMAIN_TEXT 0
#define LOG_LEVEL_DEBUG 200
#define PBL_LOG_MODULE_DEFINE(name, level)
#define PBL_LOG_MODULE_DECLARE(name, level)
#define PBL_LOG_D_DBG(domain, ...)
#define PBL_LOG_DBG(...)
#define PBL_LOG_WRN(...)
#define PBL_LOG_ERR(...)
#define PBL_LOG_INFO(...)
#define PBL_LOG_ALWAYS(fmt, ...) printk(fmt "\n", ##__VA_ARGS__)
