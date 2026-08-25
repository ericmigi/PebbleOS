/* SPDX-License-Identifier: Apache-2.0 */

#include "fw_ota_boot.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "flash_region/flash_region.h"
#include "pbl/drivers/flash.h"
#include "pbl/logging/logging.h"
#include "pbl/util/crc32.h"
#include "pfs_flash_shim.h"
#include "system/firmware_storage.h"

typedef struct FwSlot {
  uint32_t begin;
  uint32_t end;
  FirmwareHeader header;
  bool valid;
} FwSlot;

static bool prv_header_fits_region(const FirmwareHeader *header,
                                   uint32_t region_size) {
  if ((header->magic != FIRMWARE_HEADER_MAGIC) ||
      (header->header_length != sizeof(FirmwareHeader)) ||
      (header->fw_start < header->header_length) ||
      (header->fw_start > region_size) || (header->fw_length == 0U)) {
    return false;
  }

  return header->fw_length <= (region_size - header->fw_start);
}

static FwSlot prv_read_slot(uint32_t begin, uint32_t end) {
  FwSlot slot = {
      .begin = begin,
      .end = end,
      .header = firmware_storage_read_firmware_header(begin),
  };

  const uint32_t region_size = end - begin;
  slot.valid = prv_header_fits_region(&slot.header, region_size) &&
               firmware_storage_check_valid_firmware_header(begin,
                                                            &slot.header);
  return slot;
}

FwBootSelection fw_boot_select(void) {
  const FwSlot slot0 =
      prv_read_slot(FLASH_REGION_FIRMWARE_SLOT_0_BEGIN,
                    FLASH_REGION_FIRMWARE_SLOT_0_END);
  const FwSlot slot1 =
      prv_read_slot(FLASH_REGION_FIRMWARE_SLOT_1_BEGIN,
                    FLASH_REGION_FIRMWARE_SLOT_1_END);

  if (slot0.valid &&
      (!slot1.valid || (slot0.header.fw_timestamp >=
                        slot1.header.fw_timestamp))) {
    return FwBootSelectionNormalSlot0;
  }
  if (slot1.valid) {
    return FwBootSelectionNormalSlot1;
  }

  const FwSlot prf = prv_read_slot(FLASH_REGION_SAFE_FIRMWARE_BEGIN,
                                   FLASH_REGION_SAFE_FIRMWARE_END);
  return prf.valid ? FwBootSelectionPrf : FwBootSelectionNone;
}

FwBootSelection fw_boot_select_and_report(void) {
  FwBootSelection selection = fw_boot_select();
  if ((selection == FwBootSelectionNormalSlot0) ||
      (selection == FwBootSelectionNormalSlot1)) {
    PBL_LOG_ALWAYS("FW_BOOT_SLOT normal");
    return selection;
  }

  if (selection == FwBootSelectionNone) {
    // ponytail: pblboot owns the pre-jump panic when PRF is also invalid. This
    // linked recovery scaffold cannot jump before main(), so keep running it
    // while retaining the same normal-slot-to-PRF fallback decision.
    selection = FwBootSelectionPrf;
  }
  PBL_LOG_ALWAYS("FW_BOOT_SLOT prf");
  return selection;
}

static int prv_validate_blob(const uint8_t *image, size_t image_size,
                             FirmwareHeader *header_out) {
  if ((image == NULL) || (header_out == NULL) ||
      (image_size < sizeof(FirmwareHeader)) || (image_size > UINT32_MAX)) {
    return -EINVAL;
  }

  memcpy(header_out, image, sizeof(*header_out));
  if (!prv_header_fits_region(header_out, (uint32_t)image_size) ||
      ((size_t)header_out->fw_start + header_out->fw_length != image_size)) {
    return -EINVAL;
  }

  const uint32_t calculated_crc =
      crc32(CRC32_INIT, image + header_out->fw_start, header_out->fw_length);
  if (calculated_crc != header_out->fw_crc) {
    return -EBADMSG;
  }

  // ponytail: today's real pblboot image format authenticates only with CRC32.
  // Add a signed manifest, provisioned production keys, and signature checking
  // here and in pblboot before treating OTA images as trusted code.
  return 0;
}

static FwSlot prv_get_ota_target(FwOtaImageType image_type) {
  (void)image_type;
  // ponytail: every receive stages into the non-bootable OTA scratch. The
  // payloads this scaffold can build are not executable, and pblboot's header
  // format is identical to ours (same magic + priority selection), so writing
  // one into a real slot/prf would brick boot. Route firmware -> inactive slot
  // (via fw_boot_select) and recovery -> SAFE_FIRMWARE here only once images
  // are executable and signed, and pblboot handoff exists.
  return (FwSlot){
      .begin = FLASH_REGION_OTA_SCRATCH_BEGIN,
      .end = FLASH_REGION_OTA_SCRATCH_END,
  };
}

int fw_ota_receive_image(const uint8_t *image, size_t image_size,
                         FwOtaImageType image_type) {
  PBL_LOG_ALWAYS("FW_OTA_RECV");

  FirmwareHeader header;
  int result = prv_validate_blob(image, image_size, &header);
  if (result != 0) {
    return result;
  }

  if ((image_type != FwOtaImageFirmware) &&
      (image_type != FwOtaImageRecovery)) {
    return -EINVAL;
  }

  result = pfs_flash_shim_init();
  if (result != 0) {
    return result;
  }

  const FwSlot target = prv_get_ota_target(image_type);
  const uint32_t region_size = target.end - target.begin;
  if ((image_size > region_size) ||
      !prv_header_fits_region(&header, region_size)) {
    return -EFBIG;
  }

  const uint32_t erase_size =
      ((uint32_t)image_size + SUBSECTOR_SIZE_BYTES - 1U) &
      SUBSECTOR_ADDR_MASK;
  flash_region_erase_optimal_range(target.begin, target.begin,
                                   target.begin + erase_size,
                                   target.begin + erase_size);

  // Leave the header erased until the payload has passed a flash-readback CRC.
  // This makes the pblboot header itself the bootable commit marker.
  const size_t body_size = image_size - sizeof(FirmwareHeader);
  flash_write_bytes(image + sizeof(FirmwareHeader),
                    target.begin + sizeof(FirmwareHeader), body_size);

  if (!firmware_storage_check_valid_firmware_header(target.begin, &header)) {
    return -EIO;
  }
  PBL_LOG_ALWAYS("FW_OTA_VALIDATED");

  flash_write_bytes(image, target.begin, sizeof(FirmwareHeader));
  const FirmwareHeader committed =
      firmware_storage_read_firmware_header(target.begin);
  if ((memcmp(&committed, &header, sizeof(header)) != 0) ||
      !firmware_storage_check_valid_firmware_header(target.begin,
                                                    &committed)) {
    return -EIO;
  }

  PBL_LOG_ALWAYS("FW_OTA_SLOT_SET");
  // ponytail: hand the selected slot to pblboot and reset after the BLE update
  // transaction has ACKed. The scaffold deliberately does not reboot here.
  return 0;
}

int fw_ota_inject_test_image(void) {
  static const uint8_t s_payload[] = "PebbleOS Zephyr OTA test payload";
  enum { TestImageStart = 32 };
  uint8_t image[TestImageStart + sizeof(s_payload)];
  memset(image, 0xff, sizeof(image));
  memcpy(image + TestImageStart, s_payload, sizeof(s_payload));

  const FirmwareHeader header = {
      .magic = FIRMWARE_HEADER_MAGIC,
      .header_length = sizeof(FirmwareHeader),
      .fw_timestamp = (UINT64_C(0x80) << 56) | (uint32_t)FW_BUILD_EPOCH,
      .fw_start = TestImageStart,
      .fw_length = sizeof(s_payload),
      .fw_crc = crc32(CRC32_INIT, s_payload, sizeof(s_payload)),
  };
  memcpy(image, &header, sizeof(header));

  return fw_ota_receive_image(image, sizeof(image), FwOtaImageFirmware);
}

bool fw_ota_test_injection_requested(void) {
#ifdef FW_OTA_TEST_INJECT
  return true;
#else
  return false;
#endif
}
