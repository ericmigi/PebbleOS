/* SPDX-License-Identifier: Apache-2.0 */

// Board glue for the shipping SiFli QSPI flash driver on obelix. Mirrors the
// QSPI_PORT / QSPI_FLASH objects from src/fw/board/boards/board_obelix.c so the
// Zephyr fw app can drive flash through the same HAL path shipping uses, instead
// of the Zephyr flash driver (which cannot persist writes/erases at the
// filesystem region offset). See board_obelix.c for the authoritative config.

// The SiFli HAL umbrella, QSPI flash types, and the QSPI/QSPI_FLASH externs come
// from flash_port_pre.h (force-included via CMake) in the correct order.

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/sys/printk.h>

#include "flash_table.h"
#include "pfs_flash_shim.h"

// Backing key for the FreeRTOS-style critical section used by qspi.c (see
// include/port_crit.h).
unsigned int g_port_crit_key;

static QSPIPortState s_qspi_port_state = {
    .cfg = {
      .Instance = FLASH2,
      .line = HAL_FLASH_QMODE,
      .base = FLASH2_BASE_ADDR,
      .msize = 16,
      .SpiMode = SPI_MODE_NOR,
    },
    .dma = {
      .Instance = DMA1_Channel2,
      .dma_irq = DMAC1_CH2_IRQn,
      .request = DMA_REQUEST_1,
    },
    .t_enter_deep_us = 3,
    .t_exit_deep_us = 20,
};

static QSPIPort QSPI_PORT = {
    .state = &s_qspi_port_state,
    .clk_div = 0U,
};
QSPIPort *const QSPI = &QSPI_PORT;

static QSPIFlashState s_qspi_flash_state;
static QSPIFlash QSPI_FLASH_DEVICE = {
    .state = &s_qspi_flash_state,
    .qspi = &QSPI_PORT,
};
QSPIFlash *const QSPI_FLASH = &QSPI_FLASH_DEVICE;

// sifli_lrc_glue.c provides the Zephyr-backed HAL tick and calibrated
// microsecond-delay overrides shared by QSPI and BLE controller bring-up.

// Initialize the QSPI flash handle WITHOUT re-running HAL_FLASH_Init /
// HAL_QSPI_Init. The MPI2 controller is already configured for XIP by pblboot;
// re-initializing it (rewriting the read-timing registers TIMR/CIR/ABR1/HRABR)
// corrupts instruction fetch from this same flash and hangs the CPU. Instead we
// populate the handle fields the shipping erase/write path reads (Instance,
// base, size, Mode, ctable) from the known GD25Q256E identity, leaving the live
// read path untouched. dma is left NULL so writes take the FIFO path (no DMA1
// dependency). The chip stalls XIP fetches for the duration of an erase/program
// and resumes when done, so the (interrupt-locked) poll loops complete without a
// controller re-init.
void qspi_board_flash_init(void) {
  QSPIPortState *ps = QSPI->state;
  QSPI_FLASH_CTX_T *ctx = &ps->ctx;
  FLASH_HandleTypeDef *h = &ctx->handle;

  h->Instance = FLASH2;
  h->ErrorCode = 0;
  h->base = FLASH2_BASE_ADDR;
  h->size = 0x2000000u; // 32 MB
  h->Mode = HAL_FLASH_QMODE;
  h->State = HAL_FLASH_STATE_READY;
  h->isNand = 0;
  h->dualFlash = 0;
  // GD25Q256E RDID = 0x1940c8: fid=0xc8 (mfr), type=0x40, did=0x19 (capacity).
  h->ctable = spi_flash_get_cmd_by_id(0xc8u, 0x19u, 0x40u);

  ctx->dev_id = 0x1940c8u;
  ctx->flash_mode = SPI_MODE_NOR;
  ctx->dual_mode = 1;
  ctx->cache_flag = 2;
  ctx->base_addr = FLASH2_BASE_ADDR;
  ctx->total_size = 0x2000000u;

  // Configure the DMA handle exactly as HAL_FLASH_Init does, so page programs
  // take the shipping DMA path (the FIFO path needs controller state that a full
  // HAL_FLASH_Init would set up). The DMAC clock is already enabled by the Zephyr
  // dmac driver.
  h->dma = &ps->hdma;
  h->dma->Instance = DMA1_Channel2;
  h->dma->Init.Request = DMA_REQUEST_1;
  h->dma->Init.Direction = DMA_MEMORY_TO_PERIPH;
  h->dma->Init.PeriphInc = DMA_PINC_DISABLE;
  h->dma->Init.MemInc = DMA_MINC_ENABLE;
  h->dma->Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  h->dma->Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
  h->dma->Init.Mode = DMA_NORMAL;
  h->dma->Init.Priority = DMA_PRIORITY_MEDIUM;
  h->dma->Init.BurstSize = 0;
  HAL_FLASH_SET_TXSLOT(h, 1);

  ps->initialized = true;

  printk("FW_QSPI_FLASH_UP ctable=%p size=0x%x\n", (void *)h->ctable,
         (unsigned int)h->size);
}
