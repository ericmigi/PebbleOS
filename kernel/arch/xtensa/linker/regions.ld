/*
 * SPDX-FileCopyrightText: 2026 Core Devices LLC
 * SPDX-License-Identifier: Apache-2.0
 */

ENTRY(Reset_Handler)

/* No MPU: the region only has to hold what is there, as much as on ARM. */
__unpriv_ro_bss_size__ = 2K;
