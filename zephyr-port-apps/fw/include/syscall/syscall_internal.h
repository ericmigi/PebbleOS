/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#define DEFINE_SYSCALL(ret_type, function, ...) ret_type function(__VA_ARGS__)
#define PRIVILEGE_WAS_ELEVATED 0
