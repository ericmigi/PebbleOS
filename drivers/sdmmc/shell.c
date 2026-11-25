/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include "esp32s3.h"
#include "pff.h"

#include <errno.h>
#include <inttypes.h>
#include <string.h>
#ifdef CONFIG_DISPLAY_UC8279_X4
#include <pbl/drivers/display/ssd1677.h>
#endif

#include <pbl/kernel/mutex.h>
#include <pbl/shell/shell.h>

static PBL_MUTEX_DEFINE(s_lock);
static FATFS s_volume;

static int prv_probe(const struct pbl_shell *sh) {
  int ret = sd_card_probe();
  if (ret != 0) {
    const struct sd_card *c = sd_card_info();
    pbl_shell_error(sh, "Card unavailable (%d), CMD%u host status 0x%08" PRIx32, ret,
                    (unsigned)c->last_cmd, c->last_status);
  }
  return ret;
}

static int prv_info(const struct pbl_shell *sh, size_t argc, char **argv) {
  pbl_mutex_lock(&s_lock, PBL_FOREVER);
  int ret = prv_probe(sh);
  if (ret == 0) {
    const struct sd_card *c = sd_card_info();
    pbl_shell_print(sh, "%s, %" PRIu64 " sectors of 512 bytes, 1-bit 5 MHz, read-only",
                    c->block_addressed ? "SDHC/SDXC" : "SDSC", c->sectors);
    pbl_shell_print(sh, "CID %08" PRIx32 "%08" PRIx32 "%08" PRIx32 "%08" PRIx32, c->cid[3],
                    c->cid[2], c->cid[1], c->cid[0]);
  }
  pbl_mutex_unlock(&s_lock);
  return ret;
}

static int prv_volume(const struct pbl_shell *sh) {
  int ret = prv_probe(sh);
  if (ret != 0)
    return ret;
  FRESULT fr = pf_mount(&s_volume);
  if (fr != FR_OK) {
    pbl_shell_error(sh, "Cannot mount FAT12/16/32 volume (FatFs %d)", fr);
    return -EIO;
  }
  return 0;
}

static int prv_ls(const struct pbl_shell *sh, size_t argc, char **argv) {
  DIR dir;
  FILINFO file;
  pbl_mutex_lock(&s_lock, PBL_FOREVER);
  int ret = prv_volume(sh);
  if (ret == 0) {
    FRESULT fr = pf_opendir(&dir, argc == 2 ? argv[1] : "");
    while (fr == FR_OK) {
      fr = pf_readdir(&dir, &file);
      if (fr != FR_OK || file.fname[0] == '\0')
        break;
      pbl_shell_print(sh, "%s%s  %" PRIu32, file.fname, file.fattrib & AM_DIR ? "/" : "",
                      file.fsize);
    }
    if (fr != FR_OK) {
      pbl_shell_error(sh, "Directory read failed (FatFs %d)", fr);
      ret = -EIO;
    }
  }
  pbl_mutex_unlock(&s_lock);
  return ret;
}

static int prv_head(const struct pbl_shell *sh, size_t argc, char **argv) {
  uint8_t bytes[128];
  pbl_mutex_lock(&s_lock, PBL_FOREVER);
  int ret = prv_volume(sh);
  if (ret == 0) {
    FRESULT fr = pf_open(argv[1]);
    UINT n = 0;
    if (fr == FR_OK)
      fr = pf_read(bytes, sizeof(bytes), &n);
    if (fr == FR_OK) {
      for (unsigned i = 0; i < n; i += 16) {
        char hex[3 * 16 + 1] = {0};
        const char digits[] = "0123456789abcdef";
        for (unsigned j = 0; j < 16 && i + j < n; j++) {
          hex[3 * j] = digits[bytes[i + j] >> 4];
          hex[3 * j + 1] = digits[bytes[i + j] & 15];
          hex[3 * j + 2] = ' ';
        }
        pbl_shell_print(sh, "%04x  %s", i, hex);
      }
    } else {
      pbl_shell_error(sh, "File read failed (FatFs %d)", fr);
      ret = -EIO;
    }
  }
  pbl_mutex_unlock(&s_lock);
  return ret;
}

PBL_SHELL_SUBCMD_SET_CREATE(sub_sd);
PBL_SHELL_CMD_REGISTER(sd, sub_sd, "Read the SD card", nullptr);
PBL_SHELL_SUBCMD_ADD(sub_sd, info, nullptr, "Identify card and verify sector zero", prv_info, 1, 0);
PBL_SHELL_SUBCMD_ADD(sub_sd, ls, nullptr, "List a FAT directory [8.3 path]", prv_ls, 1, 1);
PBL_SHELL_SUBCMD_ADD(sub_sd, head, nullptr, "Show first 128 file bytes in hex <8.3 path>", prv_head,
                     2, 0);

#ifdef CONFIG_DISPLAY_UC8279_X4
static int prv_read_image(void *ctx, unsigned plane, unsigned gate, uint8_t *data, unsigned gates) {
  UINT count = 0;
  UINT bytes = gates * 100U;
  return pf_read(data, bytes, &count) == FR_OK && count == bytes ? 0 : -EIO;
}

static int prv_show(const struct pbl_shell *sh, size_t argc, char **argv) {
  static const uint8_t magic[] = {'X', '4', 'P', '2', 1, 0, 0, 0};
  uint8_t header[sizeof(magic)];
  UINT count = 0;
  pbl_mutex_lock(&s_lock, PBL_FOREVER);
  int ret = prv_volume(sh);
  if (ret == 0) {
    if (pf_open(argv[1]) != FR_OK || s_volume.fsize != sizeof(header) + 96000U ||
        pf_read(header, sizeof(header), &count) != FR_OK || count != sizeof(header) ||
        memcmp(header, magic, sizeof(header)) != 0) {
      ret = -EINVAL;
    } else {
      ret = pbl_display_uc8279_image(prv_read_image, nullptr);
    }
    if (ret != 0) {
      pbl_shell_error(sh, "Cannot display X4P image (%d)", ret);
    }
  }
  pbl_mutex_unlock(&s_lock);
  return ret;
}
PBL_SHELL_SUBCMD_ADD(sub_sd, show, nullptr, "Show a converted 480x800 X4P image", prv_show, 2, 0);
#endif
