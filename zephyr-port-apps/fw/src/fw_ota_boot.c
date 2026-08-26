/* SPDX-License-Identifier: Apache-2.0 */

#include "fw_ota_boot.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/sys/printk.h>

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

typedef struct FwOtaSlotState {
  uint32_t total_size;
  uint32_t written;
  uint32_t header_received;
  uint8_t header_bytes[sizeof(FirmwareHeader)];
  bool receiving;
  bool verified;
  bool header_written;
  bool installed;
} FwOtaSlotState;

static FwOtaSlotState s_ota_slot;

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

static const FwSlot s_ota_target = {
    .begin = FLASH_REGION_FIRMWARE_SLOT_1_BEGIN,
    .end = FLASH_REGION_FIRMWARE_SLOT_1_END,
};

static int prv_validate_streamed_header(FirmwareHeader *header_out) {
  if ((header_out == NULL) ||
      (s_ota_slot.header_received != sizeof(FirmwareHeader))) {
    return -EINVAL;
  }

  memcpy(header_out, s_ota_slot.header_bytes, sizeof(*header_out));
  const uint32_t region_size = s_ota_target.end - s_ota_target.begin;
  if (!prv_header_fits_region(header_out, region_size) ||
      (header_out->fw_start + header_out->fw_length !=
       s_ota_slot.total_size)) {
    return -EINVAL;
  }

  const uint32_t calculated_crc =
      flash_crc32(s_ota_target.begin + header_out->fw_start,
                  header_out->fw_length);
  return (calculated_crc == header_out->fw_crc) ? 0 : -EBADMSG;
}

int fw_ota_slot_begin(uint32_t image_size, uint32_t append_offset) {
  const uint32_t region_size = s_ota_target.end - s_ota_target.begin;
  if ((image_size < sizeof(FirmwareHeader)) || (image_size > region_size) ||
      (append_offset > image_size)) {
    return (image_size > region_size) ? -EFBIG : -EINVAL;
  }

  int result = pfs_flash_shim_init();
  if (result != 0) {
    return result;
  }

  if (append_offset != 0U) {
    if (!s_ota_slot.receiving ||
        (s_ota_slot.total_size != image_size) ||
        (s_ota_slot.written != append_offset)) {
      return -ENOTSUP;
    }
    printk("FW_OTA_RECV_BEGIN slot=0x%08x size=%u append=%u\n",
           s_ota_target.begin, image_size, append_offset);
    return 0;
  }

  s_ota_slot = (FwOtaSlotState){
      .total_size = image_size,
      .receiving = true,
  };

  const uint32_t erase_size =
      (image_size + SUBSECTOR_SIZE_BYTES - 1U) & SUBSECTOR_ADDR_MASK;
  const uint32_t erase_end = s_ota_target.begin + erase_size;
  flash_region_erase_optimal_range(s_ota_target.begin, s_ota_target.begin,
                                   erase_end, erase_end);
  printk("FW_OTA_RECV_BEGIN slot=0x%08x size=%u append=0\n",
         s_ota_target.begin, image_size);
  return 0;
}

int fw_ota_slot_write(uint32_t offset, const uint8_t *data, uint32_t length) {
  if (!s_ota_slot.receiving || (offset != s_ota_slot.written) ||
      (length > (s_ota_slot.total_size - s_ota_slot.written)) ||
      ((length != 0U) && (data == NULL))) {
    return -EINVAL;
  }

  uint32_t consumed = 0;
  if (offset < sizeof(FirmwareHeader)) {
    uint32_t header_length = sizeof(FirmwareHeader) - offset;
    if (header_length > length) {
      header_length = length;
    }
    if (offset != s_ota_slot.header_received) {
      return -EINVAL;
    }
    memcpy(s_ota_slot.header_bytes + offset, data, header_length);
    s_ota_slot.header_received += header_length;
    consumed = header_length;
  }

  if (consumed < length) {
    flash_write_bytes(data + consumed, s_ota_target.begin + offset + consumed,
                      length - consumed);
  }
  s_ota_slot.written += length;
  return 0;
}

int fw_ota_slot_finish(void) {
  if (!s_ota_slot.receiving ||
      (s_ota_slot.written != s_ota_slot.total_size)) {
    return -EINVAL;
  }

  FirmwareHeader header;
  int result = prv_validate_streamed_header(&header);
  if (result != 0) {
    return result;
  }

  s_ota_slot.receiving = false;
  s_ota_slot.verified = true;
  return 0;
}

int fw_ota_slot_install(void) {
  if (!s_ota_slot.verified || s_ota_slot.installed) {
    return -EINVAL;
  }

  FirmwareHeader header;
  int result = prv_validate_streamed_header(&header);
  if (result != 0) {
    return result;
  }

  // The header is the bootable commit marker. Keep it erased until Install,
  // after all PutBytes and in-image CRC checks have succeeded.
  flash_write_bytes(s_ota_slot.header_bytes, s_ota_target.begin,
                    sizeof(s_ota_slot.header_bytes));
  s_ota_slot.header_written = true;

  const FirmwareHeader committed =
      firmware_storage_read_firmware_header(s_ota_target.begin);
  if ((memcmp(&committed, &header, sizeof(header)) != 0) ||
      !firmware_storage_check_valid_firmware_header(s_ota_target.begin,
                                                    &committed)) {
    return -EIO;
  }

  s_ota_slot.installed = true;
  printk("FW_OTA_SLOT1_WRITTEN base=0x%08x size=%u\n", s_ota_target.begin,
         s_ota_slot.total_size);
  return 0;
}

void fw_ota_slot_abort(void) {
  if (s_ota_slot.header_written) {
    flash_region_erase_optimal_range(
        s_ota_target.begin, s_ota_target.begin,
        s_ota_target.begin + SUBSECTOR_SIZE_BYTES,
        s_ota_target.begin + SUBSECTOR_SIZE_BYTES);
  }
  s_ota_slot = (FwOtaSlotState){};
}

int fw_ota_receive_image(const uint8_t *image, size_t image_size,
                         FwOtaImageType image_type) {
  FirmwareHeader header;
  int result = prv_validate_blob(image, image_size, &header);
  if (result != 0) {
    return result;
  }

  if (image_type != FwOtaImageFirmware) {
    return -ENOTSUP;
  }

  result = fw_ota_slot_begin((uint32_t)image_size, 0);
  if (result == 0) {
    result = fw_ota_slot_write(0, image, (uint32_t)image_size);
  }
  if (result == 0) {
    result = fw_ota_slot_finish();
  }
  if (result == 0) {
    result = fw_ota_slot_install();
  }
  if (result != 0) {
    fw_ota_slot_abort();
  }
  return result;
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

  // Keep the deliberately non-executable injection out of bootable slots.
  int result = pfs_flash_shim_init();
  if (result != 0) {
    return result;
  }
  const uint32_t erase_end =
      FLASH_REGION_OTA_SCRATCH_BEGIN + SUBSECTOR_SIZE_BYTES;
  flash_region_erase_optimal_range(FLASH_REGION_OTA_SCRATCH_BEGIN,
                                   FLASH_REGION_OTA_SCRATCH_BEGIN, erase_end,
                                   erase_end);
  flash_write_bytes(image + sizeof(FirmwareHeader),
                    FLASH_REGION_OTA_SCRATCH_BEGIN + sizeof(FirmwareHeader),
                    sizeof(image) - sizeof(FirmwareHeader));
  if (!firmware_storage_check_valid_firmware_header(
          FLASH_REGION_OTA_SCRATCH_BEGIN, &header)) {
    return -EIO;
  }
  flash_write_bytes(image, FLASH_REGION_OTA_SCRATCH_BEGIN,
                    sizeof(FirmwareHeader));
  return 0;
}

bool fw_ota_test_injection_requested(void) {
#ifdef FW_OTA_TEST_INJECT
  return true;
#else
  return false;
#endif
}
