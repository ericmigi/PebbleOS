/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

// Starts controller and NimBLE-host initialization on a dedicated thread.
// The caller must invoke this only after PFS and board initialization.
void fw_ble_init(void);
