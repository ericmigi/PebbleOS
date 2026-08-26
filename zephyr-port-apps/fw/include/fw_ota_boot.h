/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum FwBootSelection {
  FwBootSelectionNone = 0,
  FwBootSelectionNormalSlot0,
  FwBootSelectionNormalSlot1,
  FwBootSelectionPrf,
} FwBootSelection;

typedef enum FwOtaImageType {
  FwOtaImageFirmware = 0,
  FwOtaImageRecovery,
} FwOtaImageType;

//! Validate both normal slots, choose the higher-priority valid image, and
//! fall back to the validated PRF region when neither normal slot is valid.
FwBootSelection fw_boot_select(void);

//! Run the scaffold boot selection and emit FW_BOOT_SLOT normal|prf.
FwBootSelection fw_boot_select_and_report(void);

//! Start streaming a pblboot-format image into firmware slot1. A zero append
//! offset erases the complete 4 KiB-rounded image span before any bytes are
//! written. A non-zero offset is accepted only for a live interrupted transfer.
int fw_ota_slot_begin(uint32_t image_size, uint32_t append_offset);

//! Append bytes at the exact next image offset. The pblboot header is retained
//! in RAM until install so an interrupted transfer cannot become bootable.
int fw_ota_slot_write(uint32_t offset, const uint8_t *data, uint32_t length);

//! Validate the complete streamed image and its in-header IEEE CRC. This leaves
//! the slot header erased; fw_ota_slot_install() is the bootable commit point.
int fw_ota_slot_finish(void);

//! Commit the retained header verbatim, then validate the bootable slot from
//! flash. Returns only after all blocking QSPI writes and readback complete.
int fw_ota_slot_install(void);

//! Abandon the current receive. If the header was already installed, erase its
//! 4 KiB subsector so pblboot cannot select the abandoned image.
void fw_ota_slot_abort(void);

//! Receive a complete pblboot-format firmware blob using the streaming slot1
//! path. This compatibility wrapper is not used by BLE PutBytes.
int fw_ota_receive_image(const uint8_t *image, size_t image_size,
                         FwOtaImageType image_type);

//! Locally inject a small CRC-valid, deliberately non-executable test image.
//! This exists to exercise receive/validation/commit without BLE transport.
int fw_ota_inject_test_image(void);

//! Runtime predicate kept across translation units so the default-off build
//! retains the callable OTA receive path in the linked image.
bool fw_ota_test_injection_requested(void);
