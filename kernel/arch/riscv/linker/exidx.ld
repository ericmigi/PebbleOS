/*
 * SPDX-FileCopyrightText: 2026 Core Devices LLC
 * SPDX-License-Identifier: Apache-2.0
 */

    /* No unwinder is linked, so the unwind tables are never read. */
    /DISCARD/ : {
        *(.eh_frame .eh_frame_hdr)
    }
