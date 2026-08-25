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

//! Receive a complete pblboot-format image blob, write it through the Zephyr
//! flash driver, validate its CRC from flash, then atomically commit its header.
int fw_ota_receive_image(const uint8_t *image, size_t image_size,
                         FwOtaImageType image_type);

//! Locally inject a small CRC-valid, deliberately non-executable test image.
//! This exists to exercise receive/validation/commit without BLE transport.
int fw_ota_inject_test_image(void);

//! Runtime predicate kept across translation units so the default-off build
//! retains the callable OTA receive path in the linked image.
bool fw_ota_test_injection_requested(void);
