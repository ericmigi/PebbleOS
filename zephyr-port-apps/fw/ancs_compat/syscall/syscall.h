/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
// Empty stand-in for the fw syscall header when building the shipping ANCS
// parser (ancs_util.c) into the Zephyr port. ancs_util.c #includes
// syscall/syscall.h but calls no sys_* function; the real port shim pulls in
// Zephyr headers whose sign_extend() clashes with pbl's. This empty header
// short-circuits that include (found first via -iquote) with no loss.
