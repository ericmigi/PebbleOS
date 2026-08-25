/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

int pfs_flash_shim_init(void);

// Populate the SiFli QSPI flash handle from the already-running controller,
// without re-initializing it. Defined in qspi_board.c.
void qspi_board_flash_init(void);
