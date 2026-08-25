/* SPDX-FileCopyrightText: 2025 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#ifdef NIMBLE_HCI_SF32LB52_TRACE_BINARY
  #include <board/board.h>
  #include <drivers/uart.h>
#endif // NIMBLE_HCI_SF32LB52_TRACE_BINARY

#include <bf0_hal.h>
#ifdef __ZEPHYR__
  // bf0_hal.h and Zephyr define IS_ALIGNED with opposite argument order.
  #undef IS_ALIGNED
  #include <zephyr/kernel.h>
  #include <zephyr/irq.h>
  // os/util.h duplicates Zephyr's pointer helper macros.
  #define H_OS_UTIL_
#else
#include <kernel/pebble_tasks.h>
#endif
#include <system/hexdump.h>
#include <pbl/logging/logging.h>
#include <system/passert.h>

// NOTE: transport.h needs os_mbuf.h to be included first
// clang-format off
#include <os/os_mbuf.h>
// clang-format on
#include <nimble/hci_common.h>
#include <nimble/transport.h>
#include <nimble/transport/hci_h4.h>
#include <nimble/transport_impl.h>
#include <os/os_mempool.h>

#include <ipc_queue.h>
#ifdef __ZEPHYR__
  #include <bf0_hal_lcpu_config.h>
  #include <bf0_hal_lrc_cal.h>
  #include <circular_buf.h>
  #include <ipc_hw.h>
#endif

#define IPC_TIMEOUT_TICKS 10

#define HCI_TRACE_HEADER_LEN 16

#define H4TL_PACKET_HOST 0x61
#define H4TL_PACKET_CTRL 0x62

#define IO_MB_CH (0)
#define TX_BUF_SIZE HCPU2LCPU_MB_CH1_BUF_SIZE
#define TX_BUF_ADDR HCPU2LCPU_MB_CH1_BUF_START_ADDR
#define TX_BUF_ADDR_ALIAS HCPU_ADDR_2_LCPU_ADDR(HCPU2LCPU_MB_CH1_BUF_START_ADDR)
#define RX_BUF_ADDR LCPU_ADDR_2_HCPU_ADDR(LCPU2HCPU_MB_CH1_BUF_START_ADDR)
#define RX_BUF_REV_B_ADDR LCPU_ADDR_2_HCPU_ADDR(LCPU2HCPU_MB_CH1_BUF_REV_B_START_ADDR)

#define BLE_HCI_EXT_SF32LB52_BLE_READY 0xFC11U

#ifdef __ZEPHYR__
#define HCI_POLL_INTERVAL_MS 50
#define HCI_POLL_REPORT_GTICKS 32768U
#define HCI_SYNC_TIMEOUT_GTICKS (5U * HCI_POLL_REPORT_GTICKS)
#define LCPU_LP_CAL_CYCLES 200U
#define HXT_READY_EXPECTED_US 1000U
#endif

#ifdef __ZEPHYR__
K_THREAD_STACK_DEFINE(s_hci_stack, 2048);
static struct k_thread s_hci_thread;
static k_tid_t s_hci_task_handle;
static struct k_sem s_ipc_data_ready;
static struct k_sem s_acl_pool_avail;
static atomic_t s_lcpu_irq_count;
static atomic_t s_hci_tx_reported;
static atomic_t s_hci_cmd_pending;
static atomic_t s_hci_sync_failed;
static atomic_t s_hci_cmd_rx_bytes;
static volatile struct circular_buf *s_rx_ring;
static uint32_t s_rx_ring_addr;
static uint32_t s_hci_tx_gtimer;
static uint32_t s_hci_poll_report_gtimer;
#else
static TaskHandle_t s_hci_task_handle;
static SemaphoreHandle_t s_ipc_data_ready;
/* Given by prv_acl_put_signal() whenever an LL-direction ACL mbuf is returned
 * to the transport pool; taken by the HCI RX task when an alloc fails so we
 * unblock as soon as the host has freed something instead of polling.
 */
static SemaphoreHandle_t s_acl_pool_avail;
#endif
static struct hci_h4_sm s_hci_h4sm;
static ipc_queue_handle_t s_ipc_port;
static int s_transport_status;
static const char *s_transport_failure_where;

#ifdef __ZEPHYR__
extern void LCPU2HCPU_IRQHandler(void);
extern void ipc_queue_data_ind(uint32_t user_data);
void ble_transport_sf32lb52_dump_ipc(void);
void ble_transport_sf32lb52_report_sync_timeout(void);

static void prv_lcpu2hcpu_isr(const void *arg) {
  (void)arg;
  atomic_inc(&s_lcpu_irq_count);
  LCPU2HCPU_IRQHandler();
}

static void prv_poll_mailbox(void) {
  MAILBOX_HandleTypeDef *mailbox = &ipc_hw_obj.ch[0].cfg.rx.handle;

  /* HAL_MAILBOX_IRQHandler clears MISR and dispatches ipc_queue_data_ind().
   * Run it from the poller too so receiving does not depend on IRQ 58.
   */
  if (__HAL_MAILBOX_GET_STATUS(mailbox) != 0U) {
    HAL_MAILBOX_IRQHandler(mailbox);
  }

  /* ipc_queue_read() is gated by the queue's cached data_len. Refresh it
   * directly as well, so polling still works when the mailbox IRQ is lost or
   * was cleared before its callback ran.
   */
  ipc_queue_data_ind((uint32_t)s_ipc_port);
}

static int prv_prepare_lcpu_clock(void) {
  HAL_StatusTypeDef status;
  int cal_result;
  uint32_t average_cycles;
  uint32_t current_cycles;
  uint32_t cycle_us;
  float current_frequency;

  printk("BLE_REVID 0x%02x\n", (unsigned int)__HAL_SYSCFG_GET_REVID());

  /* Match the SF32LB52 HAL_PreInit order. Keep this outer wake reference
   * held across lcpu_power_on(); its nested wake/cancel pair then cannot let
   * LPSYS sleep while the controller bring-up is under diagnosis.
   */
  status = HAL_HPAON_WakeCore(CORE_ID_LCPU);
  if (status != HAL_OK) {
    s_transport_failure_where = "lcpu_wake";
    return status;
  }

  HAL_RCC_Reset_and_Halt_LCPU(1);
  status = HAL_HPAON_StartGTimer();
  if (status != HAL_OK) {
    s_transport_failure_where = "lp_gtimer";
    return status;
  }

  status = HAL_PMU_EnableRC32K(1);
  if (status != HAL_OK) {
    s_transport_failure_where = "rc32_enable";
    return status;
  }
  status = HAL_PMU_RC32KReady();
  if (status != HAL_OK) {
    s_transport_failure_where = "rc32_ready";
    return status;
  }
  status = HAL_PMU_LpCLockSelect(PMU_LPCLK_RC32);
  if (status != HAL_OK) {
    s_transport_failure_where = "rc32_select";
    return status;
  }
  status = HAL_PMU_EnableDLL(1);
  if (status != HAL_OK) {
    s_transport_failure_where = "lp_dll";
    return status;
  }
  HAL_RCC_LCPU_ClockSelect(RCC_CLK_MOD_LP_PERI, RCC_CLK_PERI_HXT48);

  status = HAL_PMU_RC10Kconfig();
  if (status != HAL_OK) {
    s_transport_failure_where = "rc10_config";
    return status;
  }

  printk("BLE_LXT_CAL_START\n");
  cal_result = HAL_RC_CAL_update_reference_cycle_on_48M(LCPU_LP_CAL_CYCLES);
  if (cal_result < 0) {
    s_transport_failure_where = "rc10_cal";
    return cal_result;
  }

  current_cycles = HAL_RC_CAL_get_reference_cycle_on_48M();
  average_cycles = HAL_RC_CAL_get_average_cycle_on_48M();
  if (current_cycles == 0U || average_cycles == 0U) {
    s_transport_failure_where = "rc10_cycles";
    return -1;
  }

  cycle_us = current_cycles / (48U * LCPU_LP_CAL_CYCLES);
  if (cycle_us == 0U) {
    s_transport_failure_where = "rc10_period";
    return -1;
  }
  HAL_PMU_SET_HXT3_RDY_DELAY(HXT_READY_EXPECTED_US / cycle_us + 1U);

  /* The calibration routine normally publishes these values. Set both
   * explicitly because the retained BT_RC_CAL_IN_L value can otherwise make
   * it skip LPCYCLE_AVE before lcpu_rom_config_default() refreshes that flag.
   */
  status = HAL_LCPU_CONFIG_set(HAL_LCPU_CONFIG_LPCYCLE_AVE, &average_cycles,
                               sizeof(average_cycles));
  if (status != HAL_OK) {
    s_transport_failure_where = "lpcycle_ave";
    return status;
  }
  current_frequency = (48000000.0f * LCPU_LP_CAL_CYCLES) / current_cycles;
  status = HAL_LCPU_CONFIG_set(HAL_LCPU_CONFIG_LPCYCLE_CURR, &current_frequency,
                               sizeof(current_frequency));
  if (status != HAL_OK) {
    s_transport_failure_where = "lpcycle_curr";
    return status;
  }

  printk("BLE_LXT_CAL_OK %u %u\n", (unsigned int)current_cycles,
         (unsigned int)average_cycles);
  return 0;
}
#endif

static struct os_mbuf *prv_alloc_acl_from_ll(void) {
  struct os_mbuf *om = ble_transport_alloc_acl_from_ll();
  if (om != NULL) {
    return om;
  }

  /* ACL pool exhausted. Block instead of returning NULL (which the H4 state
   * machine would treat as a fatal, unrecoverable framing error). While we
   * wait we stop draining the LCPU->HCPU IPC ring; it fills and backpressures
   * the controller, i.e. transport-level flow control. prv_acl_put_signal()
   * wakes us when the host returns a buffer to the pool; the timeout guards
   * the give-before-wait race and the fact that the from_hs TX path shares
   * mpool_acl, so a freed buffer may be taken before we retry.
   */
  PBL_LOG_D_DBG(LOG_DOMAIN_BT_STACK, "ACL pool empty, waiting for buffer");
  do {
#ifdef __ZEPHYR__
    (void)k_sem_take(&s_acl_pool_avail, K_MSEC(100));
#else
    (void)xSemaphoreTake(s_acl_pool_avail, pdMS_TO_TICKS(100));
#endif
    om = ble_transport_alloc_acl_from_ll();
  } while (om == NULL);

  return om;
}

static os_error_t prv_acl_put_signal(struct os_mempool_ext *mpe, void *data, void *arg) {
  os_error_t err = os_memblock_put_from_cb(&mpe->mpe_mp, data);
#ifdef __ZEPHYR__
  k_sem_give(&s_acl_pool_avail);
#else
  if (s_acl_pool_avail != NULL) {
    (void)xSemaphoreGive(s_acl_pool_avail);
  }
#endif
  return err;
}

static void *prv_alloc_evt(int discardable) {
  void *buf = ble_transport_alloc_evt(discardable);
  if (!buf) {
    PBL_LOG_ERR("EVT alloc failed (discardable=%d)", discardable);
  }

  return buf;
}

static const struct hci_h4_allocators s_hci_h4_allocs_from_ll = {
  .acl = prv_alloc_acl_from_ll,
  .evt = prv_alloc_evt,
};

#if defined(NIMBLE_HCI_SF32LB52_TRACE_BINARY)
static uint16_t s_hci_trace_seq;
#endif

#if defined(NIMBLE_HCI_SF32LB52_TRACE_BINARY) || defined(NIMBLE_HCI_SF32LB52_TRACE_LOG)
#define MAX_HCI_PKT_SIZE 1024
static uint8_t s_hci_buf[MAX_HCI_PKT_SIZE];
#endif

extern uint8_t lcpu_power_on(void);
extern uint8_t lcpu_power_off(void);
extern void lcpu_custom_nvds_config(void);

#if defined(NIMBLE_HCI_SF32LB52_TRACE_LOG)
void prv_hci_trace(uint8_t type, const uint8_t *data, uint16_t len, uint8_t h4tl_packet) {
  const char *type_str;

  switch (type) {
  case HCI_H4_CMD:
    type_str = "CMD";
    break;
  case HCI_H4_ACL:
    type_str = "ACL";
    break;
  case HCI_H4_EVT:
    type_str = "EVT";
    break;
  case HCI_H4_ISO:
    type_str = "ISO";
    break;
  default:
    type_str = "UKN";
    break;
  }

  PBL_LOG_D_DBG(LOG_DOMAIN_BT_STACK, "%s, %s %" PRIu16, type_str,
            (h4tl_packet == H4TL_PACKET_HOST) ? "TX" : "RX", len);
  PBL_HEXDUMP_D(LOG_DOMAIN_BT_STACK, LOG_LEVEL_DEBUG, data, len);
}
#elif defined(NIMBLE_HCI_SF32LB52_TRACE_BINARY)
void prv_hci_trace(uint8_t type, const uint8_t *data, uint16_t len, uint8_t h4tl_packet) {
  uint8_t trace_hdr[HCI_TRACE_HEADER_LEN];

  // Magic for Pebble HCI, 'PBTS'
  trace_hdr[0] = 0x50U;
  trace_hdr[1] = 0x42U;
  trace_hdr[2] = 0x54U;
  trace_hdr[3] = 0x53U;
  trace_hdr[4] = 0x06U;
  trace_hdr[5] = 0x01U;
  trace_hdr[6] = (len + 8U) & 0xFFU;
  trace_hdr[7] = (len + 8U) >> 8U;
  trace_hdr[8] = s_hci_trace_seq & 0xFFU;
  trace_hdr[9] = s_hci_trace_seq >> 8U;
  trace_hdr[10] = 0U;
  trace_hdr[11] = 0U;
  trace_hdr[12] = 0U;
  trace_hdr[13] = 0U;
  trace_hdr[14] = h4tl_packet;
  trace_hdr[15] = type;

  s_hci_trace_seq++;

  for (uint8_t i = 0U; i < HCI_TRACE_HEADER_LEN; i++) {
    uart_write_byte(HCI_TRACE_UART, trace_hdr[i]);
  }

  for (uint16_t i = 0U; i < len; i++) {
    uart_write_byte(HCI_TRACE_UART, data[i]);
  }
}
#else
#define prv_hci_trace(type, data, len, h4tl_packet)
#endif

#if defined(NIMBLE_HCI_SF32LB52_TRACE_BINARY) || defined(NIMBLE_HCI_SF32LB52_TRACE_LOG)
void prv_hci_trace_mbuf(uint8_t type, struct os_mbuf *om, uint8_t h4tl_packet)  {
  PBL_ASSERTN(os_mbuf_len(om) < MAX_HCI_PKT_SIZE);
  os_mbuf_copydata(om, 0, os_mbuf_len(om), s_hci_buf);
  prv_hci_trace(type, s_hci_buf, os_mbuf_len(om), h4tl_packet);
}
#else
#define prv_hci_trace_mbuf(type, om, h4tl_packet)
#endif


static int32_t prv_ipc_rx_ind(ipc_queue_handle_t handle, size_t size) {
#ifdef __ZEPHYR__
  k_sem_give(&s_ipc_data_ready);
#else
  BaseType_t woken;

  xSemaphoreGiveFromISR(s_ipc_data_ready, &woken);
  portEND_SWITCHING_ISR(woken);
#endif

  return 0;
}

static int prv_config_ipc(void) {
  ipc_queue_cfg_t q_cfg = {0};
  int32_t ret;

  q_cfg.qid = IO_MB_CH;
  q_cfg.tx_buf_size = TX_BUF_SIZE;
  q_cfg.tx_buf_addr = TX_BUF_ADDR;
  q_cfg.tx_buf_addr_alias = TX_BUF_ADDR_ALIAS;

  uint8_t rev_id = __HAL_SYSCFG_GET_REVID();
  if (rev_id < HAL_CHIP_REV_ID_A4) {
    q_cfg.rx_buf_addr = RX_BUF_ADDR;
  } else {
    q_cfg.rx_buf_addr = RX_BUF_REV_B_ADDR;
  }
#ifdef __ZEPHYR__
  s_rx_ring_addr = q_cfg.rx_buf_addr;
  s_rx_ring = (volatile struct circular_buf *)q_cfg.rx_buf_addr;
  printk("BLE_IPC_RX_ADDR 0x%08x %s\n", (unsigned int)s_rx_ring_addr,
         rev_id < HAL_CHIP_REV_ID_A4 ? "LEGACY" : "REV_B");
  printk("BLE_IPC_ADDRS revid=0x%02x a4=0x%02x legacy=0x%08x "
         "rev_b=0x%08x selected=0x%08x\n",
         (unsigned int)rev_id, (unsigned int)HAL_CHIP_REV_ID_A4,
         (unsigned int)RX_BUF_ADDR, (unsigned int)RX_BUF_REV_B_ADDR,
         (unsigned int)q_cfg.rx_buf_addr);
#endif

  q_cfg.rx_ind = NULL;
  q_cfg.user_data = 0;

#ifdef __ZEPHYR__
  IRQ_CONNECT(LCPU2HCPU_IRQn, 5, prv_lcpu2hcpu_isr, NULL, 0);
#endif

  if (q_cfg.rx_ind == NULL) {
    q_cfg.rx_ind = prv_ipc_rx_ind;
  }

  s_ipc_port = ipc_queue_init(&q_cfg);
  if (s_ipc_port == IPC_QUEUE_INVALID_HANDLE) {
    PBL_LOG_D_ERR(LOG_DOMAIN_BT_STACK, "ipc_queue_init failed");
    s_transport_failure_where = "ipc_init";
    return -1;
  }

  ret = ipc_queue_open(s_ipc_port);
  if (ret != 0) {
    PBL_LOG_D_ERR(LOG_DOMAIN_BT_STACK, "ipc_queue_open failed (%" PRId32 ")", ret);
    s_transport_failure_where = "ipc_open";
    return ret;
  }

#ifndef __ZEPHYR__
  NVIC_EnableIRQ(LCPU2HCPU_IRQn);
  NVIC_SetPriority(LCPU2HCPU_IRQn, 5);
#else
  /* ipc_queue_open() is the legacy protocol used by PebbleOS. It unmasks the
   * H2L sender channel, but not the local L2H receiver channel. Zephyr does
   * not have a mailbox device that performs this setup for us.
   */
  __HAL_MAILBOX_UNMASK_CHANNEL_IT(&ipc_hw_obj.ch[0].cfg.rx.handle, IO_MB_CH);
  // ipc_queue_open() enables this through its Zephyr OS port. Do it
  // explicitly as well so Zephyr's NVIC state is unambiguous.
  irq_enable(LCPU2HCPU_IRQn);
  if (!irq_is_enabled(LCPU2HCPU_IRQn)) {
    s_transport_failure_where = "ipc_irq_enable";
    return -1;
  }
  printk("BLE_HCI_IRQ_CFG %d enabled=%d\n", (int)LCPU2HCPU_IRQn,
         irq_is_enabled(LCPU2HCPU_IRQn) ? 1 : 0);
  printk("BLE_IPC_OK\n");
#endif

  return 0;
}

static int prv_hci_frame_cb(uint8_t pkt_type, void *data) {
  struct ble_hci_ev *ev;
  struct ble_hci_ev_command_complete *cmd_complete;

  switch (pkt_type) {
  case HCI_H4_EVT:
    ev = data;
    cmd_complete = (void *)ev->data;

    if (ev->opcode == BLE_HCI_EVCODE_COMMAND_COMPLETE) {
      PBL_LOG_D_DBG(LOG_DOMAIN_BT_STACK, "CMD complete %x", cmd_complete->opcode);
      // NOTE: do not confuse NimBLE with SF32LB52 vendor specific command
      if (cmd_complete->opcode == BLE_HCI_EXT_SF32LB52_BLE_READY) {
        break;
      }
#ifdef __ZEPHYR__
      atomic_clear(&s_hci_cmd_pending);
#endif
    }
#ifdef __ZEPHYR__
    if (ev->opcode == BLE_HCI_EVCODE_COMMAND_STATUS) {
      atomic_clear(&s_hci_cmd_pending);
    }
#endif

    prv_hci_trace(pkt_type, data, ev->length + sizeof(*ev), H4TL_PACKET_CTRL);

    return ble_transport_to_hs_evt(data);
  case HCI_H4_ACL:
    prv_hci_trace(pkt_type, OS_MBUF_DATA((struct os_mbuf *)data, uint8_t *),
                  OS_MBUF_PKTLEN((struct os_mbuf *)data), H4TL_PACKET_CTRL);

    return ble_transport_to_hs_acl(data);
  default:
    WTF;
    break;
  }

  return -1;
}

#ifdef __ZEPHYR__
static void prv_hci_task_main(void *unused, void *unused2, void *unused3) {
  bool irq_reported = false;
  bool first_polled_rx = true;

  (void)unused2;
  (void)unused3;
  printk("BLE_HCI_TASK_UP\n");
#else
static void prv_hci_task_main(void *unused) {
#endif
  uint8_t buf[64];

  while (true) {
#ifdef __ZEPHYR__
    (void)k_sem_take(&s_ipc_data_ready, K_MSEC(HCI_POLL_INTERVAL_MS));

    /* Drain on every pass, including semaphore timeouts. Dispatch a pending
     * mailbox notification and refresh the IPC queue's cached RX length so
     * neither a missed IRQ nor a missed callback can strand bytes in SRAM.
     */
    prv_poll_mailbox();
    (void)k_sem_take(&s_ipc_data_ready, K_NO_WAIT);
    if (!irq_reported && atomic_get(&s_lcpu_irq_count) > 0) {
      irq_reported = true;
      printk("BLE_HCI_IRQ %d\n", (int)atomic_get(&s_lcpu_irq_count));
    }
#else
    xSemaphoreTake(s_ipc_data_ready, portMAX_DELAY);
#endif

    while (true) {
      size_t len;

      len = ipc_queue_read(s_ipc_port, buf, sizeof(buf));
      if (len > 0U) {
#ifdef __ZEPHYR__
        atomic_add(&s_hci_cmd_rx_bytes, len);
        if (first_polled_rx) {
          printk("BLE_POLL_RX %u\n", (unsigned int)len);
          first_polled_rx = false;
        }
        printk("BLE_HCI_RX %u\n", (unsigned int)len);
#endif
        uint8_t *pbuf = buf;
        while (len > 0U) {
          int consumed_bytes;

          /* prv_alloc_acl_from_ll() blocks until a buffer is available, so the
           * H4 state machine never reports an OOM on the ACL path; any
           * non-positive return here is unexpected.
           */
          consumed_bytes = hci_h4_sm_rx(&s_hci_h4sm, pbuf, len);
          if (consumed_bytes <= 0) {
            PBL_LOG_D_ERR(LOG_DOMAIN_BT_STACK, "hci_h4_sm_rx returned %d", consumed_bytes);
            break;
          }
          len -= consumed_bytes;
          pbuf += consumed_bytes;
        }
      } else {
        break;
      }
    }
#ifdef __ZEPHYR__
    if (atomic_get(&s_hci_cmd_pending) != 0 &&
        atomic_get(&s_hci_cmd_rx_bytes) == 0) {
      uint32_t now = HAL_HPAON_READ_GTIMER();

      if ((uint32_t)(now - s_hci_poll_report_gtimer) >=
          HCI_POLL_REPORT_GTICKS) {
        s_hci_poll_report_gtimer = now;
        printk("BLE_POLL_TICK\n");
      }
      if ((uint32_t)(now - s_hci_tx_gtimer) >= HCI_SYNC_TIMEOUT_GTICKS) {
        ble_transport_sf32lb52_report_sync_timeout();
      }
    }
#endif
  }
}

void ble_transport_ll_reinit(void) {
  int ret;

  hci_h4_sm_init(&s_hci_h4sm, &s_hci_h4_allocs_from_ll, prv_hci_frame_cb);

#ifdef __ZEPHYR__
  ret = prv_prepare_lcpu_clock();
  if (ret != 0) {
    s_transport_status = ret;
    return;
  }
#endif

  ret = prv_config_ipc();
  if (ret != 0) {
    s_transport_status = ret;
    return;
  }

  lcpu_custom_nvds_config();
#ifdef __ZEPHYR__
  printk("BLE_LCPU_CFG_OK\n");
  /* Do NOT touch the rev_b RX ring control at 0x20402800 here. On rev_b the
   * whole LPSYS RAM is only 11KB (0x20400000..0x20402BFF) and this ring sits in
   * its top 1KB, which the LCPU ROM/patch owns and initializes itself (per
   * ipc_queue.c the receiver never inits the ring; the sender does). A former
   * diagnostic zeroed 0x20 bytes here as an "LCPU execution probe"; it was inert
   * only because the HCPU D-cache swallowed the write. Once CONFIG_DCACHE=n made
   * HCPU writes reach LPSYS RAM, that memset corrupted LCPU boot RAM and
   * lcpu_power_on() hung before release -> persistent BLE_RXRING_ZEROED stall. */
#endif
  ret = lcpu_power_on();
  if (ret != 0) {
    s_transport_failure_where = "lcpu_power_on";
    s_transport_status = ret;
    return;
  }
#ifdef __ZEPHYR__
  printk("BLE_LCPU_ON\n");

  // Shared-RAM readback probe: verify HCPU writes to LPSYS RAM are visible.
  // If readback mismatches OR the just-installed rev_b patch region is empty,
  // the LPSYS RAM at 0x2040_0000 is not mapped/reserved for the HCPU.
  // The LCPU (the RX-ring sender per ipc_queue.c) writes rd_buf=addr+0x14 into
  // the rev_b RX ring control at 0x20402800 once it has run its IPC init and is
  // ready to service HCI. The ring was zeroed before ReleaseLCPU, so wait for
  // that signal before returning: if the host starts sending HCI (and ringing
  // the H2L doorbell) before the LCPU has enabled its mailbox IRQ, the command
  // is lost and the controller never responds. This is the controller-ready
  // handshake the standalone port otherwise lacks.
  {
    volatile uint32_t *rd_buf = (volatile uint32_t *)(uintptr_t)0x20402800U;
    int waited_ms = 0;
    while (*rd_buf != 0x20402814U && waited_ms < 500) {
      k_msleep(5);
      waited_ms += 5;
    }
    printk("BLE_LCPU_READY rd_buf=0x%08x waited=%dms slp_ctrl=0x%08x\n",
           (unsigned int)*rd_buf, waited_ms,
           (unsigned int)hwp_lpsys_aon->SLP_CTRL);
  }
#endif
}

void ble_transport_ll_init(void) {
#ifdef NIMBLE_HCI_SF32LB52_TRACE_BINARY
  uart_init_tx_only(HCI_TRACE_UART);
  uart_set_baud_rate(HCI_TRACE_UART, 1000000);
#endif

#ifdef __ZEPHYR__
  k_sem_init(&s_ipc_data_ready, 0, 1);
  k_sem_init(&s_acl_pool_avail, 0, 1);
  atomic_clear(&s_lcpu_irq_count);
  atomic_clear(&s_hci_tx_reported);
  atomic_clear(&s_hci_cmd_pending);
  atomic_clear(&s_hci_sync_failed);
  atomic_clear(&s_hci_cmd_rx_bytes);
  s_rx_ring = NULL;
  s_rx_ring_addr = 0;
  s_hci_tx_gtimer = 0;
  s_hci_poll_report_gtimer = 0;
#else
  s_ipc_data_ready = xSemaphoreCreateBinary();
  s_acl_pool_avail = xSemaphoreCreateBinary();
  PBL_ASSERTN(s_ipc_data_ready != NULL && s_acl_pool_avail != NULL);
#endif

  s_transport_status = 0;
  s_transport_failure_where = NULL;
  ble_transport_ll_reinit();
  if (s_transport_status != 0) {
    return;
  }

  ble_transport_register_put_acl_from_ll_cb(prv_acl_put_signal);

#ifdef __ZEPHYR__
  s_hci_task_handle = k_thread_create(&s_hci_thread, s_hci_stack,
                                      K_THREAD_STACK_SIZEOF(s_hci_stack),
                                      prv_hci_task_main,
                                      NULL, NULL, NULL, 4, 0, K_FOREVER);
  k_thread_name_set(s_hci_task_handle, "NimbleHCI");
  k_thread_start(s_hci_task_handle);
#else
  TaskParameters_t task_params = {
    .pvTaskCode = prv_hci_task_main,
    .pcName = "NimbleHCI",
    .usStackDepth = 1024 / sizeof(StackType_t),
    .uxPriority = (tskIDLE_PRIORITY + 3) | portPRIVILEGE_BIT,
    .puxStackBuffer = NULL,
  };

  pebble_task_create(PebbleTask_BTHCI, &task_params, &s_hci_task_handle);
  PBL_ASSERTN(s_hci_task_handle);
#endif
}

int ble_transport_sf32lb52_status(void) {
  return s_transport_status;
}

const char *ble_transport_sf32lb52_failure_where(void) {
  return s_transport_failure_where;
}

#ifdef __ZEPHYR__
static void prv_dump_ring_ctrl(const char *name, uint32_t addr,
                               size_t cached_len) {
  volatile const struct circular_buf *ring =
      (volatile const struct circular_buf *)addr;
  uint32_t read;
  uint32_t write;

  __DMB();
  read = ring->read_idx_mirror;
  write = ring->write_idx_mirror;
  printk("BLE_RING_CTRL %s addr=0x%08x rd_buf=0x%08x wr_buf=0x%08x "
         "read=0x%08x read_idx=%u read_mirror=%u write=0x%08x "
         "write_idx=%u write_mirror=%u size=%d cached=%u\n",
         name, (unsigned int)addr, (unsigned int)(uintptr_t)ring->rd_buffer_ptr,
         (unsigned int)(uintptr_t)ring->wr_buffer_ptr, (unsigned int)read,
         (unsigned int)CB_GET_PTR_IDX(read),
         (unsigned int)CB_GET_PTR_MIRROR(read), (unsigned int)write,
         (unsigned int)CB_GET_PTR_IDX(write),
         (unsigned int)CB_GET_PTR_MIRROR(write), (int)ring->buffer_size,
         (unsigned int)cached_len);
}

static void prv_dump_ring_bytes(const char *name, uint32_t addr) {
  volatile const struct circular_buf *ring =
      (volatile const struct circular_buf *)addr;
  volatile const uint8_t *data = (volatile const uint8_t *)(ring + 1);

  printk("BLE_RING_BYTES %s addr=0x%08x", name, (unsigned int)(uintptr_t)data);
  for (size_t i = 0; i < 16U; i++) {
    printk(" %02x", data[i]);
  }
  printk("\n");
}

void ble_transport_sf32lb52_dump_ipc(void) {
  MAILBOX_CH_TypeDef *rx_mailbox = ipc_hw_obj.ch[0].cfg.rx.handle.Instance;
  MAILBOX_CH_TypeDef *tx_mailbox = ipc_hw_obj.ch[0].cfg.tx.handle.Instance;

  if (s_rx_ring != NULL) {
    size_t cached_len = ipc_queue_get_rx_size(s_ipc_port);

    __DMB();
    printk("BLE_RING rx_addr=0x%08x read=0x%08x write=0x%08x\n",
           (unsigned int)s_rx_ring_addr,
           (unsigned int)s_rx_ring->read_idx_mirror,
           (unsigned int)s_rx_ring->write_idx_mirror);
    prv_dump_ring_ctrl("selected", s_rx_ring_addr, cached_len);
    if (s_rx_ring_addr != RX_BUF_REV_B_ADDR) {
      prv_dump_ring_ctrl("rev_b", RX_BUF_REV_B_ADDR, 0U);
    }
    if (s_rx_ring_addr != RX_BUF_ADDR) {
      prv_dump_ring_ctrl("legacy", RX_BUF_ADDR, 0U);
    }
    prv_dump_ring_ctrl("tx", TX_BUF_ADDR, 0U);
    prv_dump_ring_bytes("selected", s_rx_ring_addr);
    if (s_rx_ring_addr != RX_BUF_REV_B_ADDR) {
      prv_dump_ring_bytes("rev_b", RX_BUF_REV_B_ADDR);
    }
    if (s_rx_ring_addr != RX_BUF_ADDR) {
      prv_dump_ring_bytes("legacy", RX_BUF_ADDR);
    }
    prv_dump_ring_bytes("tx", TX_BUF_ADDR);
  } else {
    printk("BLE_RING unavailable\n");
  }
  printk("BLE_MBOX status=0x%08x\n", (unsigned int)rx_mailbox->CxMISR);
  printk("BLE_RX_MBOX ier=0x%08x itr=0x%08x isr=0x%08x misr=0x%08x\n",
         (unsigned int)rx_mailbox->CxIER, (unsigned int)rx_mailbox->CxITR,
         (unsigned int)rx_mailbox->CxISR, (unsigned int)rx_mailbox->CxMISR);
  printk("BLE_TX_MBOX ier=0x%08x itr=0x%08x isr=0x%08x misr=0x%08x\n",
         (unsigned int)tx_mailbox->CxIER, (unsigned int)tx_mailbox->CxITR,
         (unsigned int)tx_mailbox->CxISR, (unsigned int)tx_mailbox->CxMISR);
  printk("BLE_IRQ_STATE enabled=%d num=%d\n",
         irq_is_enabled(LCPU2HCPU_IRQn) ? 1 : 0, (int)LCPU2HCPU_IRQn);
}

void ble_transport_sf32lb52_report_sync_timeout(void) {
  if (atomic_get(&s_hci_cmd_pending) != 0 &&
      atomic_get(&s_hci_cmd_rx_bytes) == 0 &&
      atomic_cas(&s_hci_sync_failed, 0, 1)) {
    atomic_clear(&s_hci_cmd_pending);
    printk("BLE_FAIL hci_sync_timeout\n");
    ble_transport_sf32lb52_dump_ipc();
  }
}
#endif

void ble_transport_ll_deinit(void) {
  NVIC_DisableIRQ(LCPU2HCPU_IRQn);
  ipc_queue_close(s_ipc_port);
  ipc_queue_deinit(s_ipc_port);
  s_ipc_port = IPC_QUEUE_INVALID_HANDLE;
  // lcpu_power_off() touches LPSYS_AON registers, which are only reachable while
  // the HP→LP wake request is held. Without this the second invocation faults
  // because the LP system is asleep after the prior power-on cancelled its request.
  HAL_HPAON_WakeCore(CORE_ID_LCPU);
  lcpu_power_off();
  HAL_HPAON_CANCEL_LP_ACTIVE_REQUEST();
}

/* APIs to be implemented by HS/LL side of transports */
int ble_transport_to_ll_cmd_impl(void *buf) {
  struct ble_hci_cmd *cmd = buf;
  uint8_t h4_cmd = HCI_H4_CMD;
  size_t written;
  int err = 0;
#ifdef __ZEPHYR__
  MAILBOX_CH_TypeDef *tx_mailbox = ipc_hw_obj.ch[0].cfg.tx.handle.Instance;
  uint32_t tx_itr_before = tx_mailbox->CxITR;
  uint32_t tx_isr_before = tx_mailbox->CxISR;
#endif

  prv_hci_trace(HCI_H4_CMD, (uint8_t *)cmd, sizeof(*cmd) + cmd->length, H4TL_PACKET_HOST);

  written = ipc_queue_write(s_ipc_port, &h4_cmd, 1, IPC_TIMEOUT_TICKS);
  if (written != 1U) {
    PBL_LOG_ERR("Failed to write HCI CMD header");
    err = BLE_ERR_MEM_CAPACITY;
    goto exit;
  }

  written = ipc_queue_write(s_ipc_port, cmd, sizeof(*cmd) + cmd->length,
                            IPC_TIMEOUT_TICKS);
  if (written != sizeof(*cmd) + cmd->length) {
    PBL_LOG_ERR("Failed to write HCI CMD data");
    err = BLE_ERR_MEM_CAPACITY;
    goto exit;
  }

#ifdef __ZEPHYR__
  s_hci_tx_gtimer = HAL_HPAON_READ_GTIMER();
  s_hci_poll_report_gtimer = s_hci_tx_gtimer;
  atomic_clear(&s_hci_cmd_rx_bytes);
  atomic_set(&s_hci_cmd_pending, 1);
  if (atomic_cas(&s_hci_tx_reported, 0, 1)) {
    printk("BLE_HCI_TX %04x %u\n", (unsigned int)cmd->opcode,
           (unsigned int)(sizeof(*cmd) + cmd->length));
    __DMB();
    printk("BLE_TX_SIGNALED writes=2 before_itr=0x%08x before_isr=0x%08x "
           "ier=0x%08x itr=0x%08x isr=0x%08x misr=0x%08x\n",
           (unsigned int)tx_itr_before, (unsigned int)tx_isr_before,
           (unsigned int)tx_mailbox->CxIER, (unsigned int)tx_mailbox->CxITR,
           (unsigned int)tx_mailbox->CxISR, (unsigned int)tx_mailbox->CxMISR);
    ble_transport_sf32lb52_dump_ipc();
  }
  k_sem_give(&s_ipc_data_ready);
#endif

exit:
  ble_transport_free(buf);

  return err;
}

int ble_transport_to_ll_acl_impl(struct os_mbuf *om) {
  size_t written;
  uint8_t h4_cmd = HCI_H4_ACL;
  struct os_mbuf *x;
  int err = 0;

  prv_hci_trace_mbuf(HCI_H4_ACL, om, H4TL_PACKET_HOST);

  written = ipc_queue_write(s_ipc_port, &h4_cmd, 1U, IPC_TIMEOUT_TICKS);
  if (written != 1U) {
    PBL_LOG_ERR("Failed to write HCI ACL header");
    err = BLE_ERR_MEM_CAPACITY;
    goto exit;
  }

  x = om;
  while (x != NULL) {
    written = ipc_queue_write(s_ipc_port, x->om_data, x->om_len, IPC_TIMEOUT_TICKS);
    if (written != x->om_len) {
      PBL_LOG_ERR("Failed to write HCI ACL data");
      err = BLE_ERR_MEM_CAPACITY;
      goto exit;
    }

    x = SLIST_NEXT(x, om_next);
  }

exit:
  os_mbuf_free_chain(om);

  return err;
}

int ble_transport_to_ll_iso_impl(struct os_mbuf *om) {
  size_t written;
  uint8_t h4_cmd = HCI_H4_ISO;
  int err = 0;
  struct os_mbuf *x;

  prv_hci_trace_mbuf(HCI_H4_ISO, om, H4TL_PACKET_HOST);

  written = ipc_queue_write(s_ipc_port, &h4_cmd, 1, IPC_TIMEOUT_TICKS);
  if (written != 1U) {
    PBL_LOG_ERR("Failed to write HCI ISO header");
    err = BLE_ERR_MEM_CAPACITY;
    goto exit;
  }

  x = om;
  while (x != NULL) {
    written = ipc_queue_write(s_ipc_port, x->om_data, x->om_len, IPC_TIMEOUT_TICKS);
    if (written != x->om_len) {
      PBL_LOG_ERR("Failed to write HCI ISO data");
      err = BLE_ERR_MEM_CAPACITY;
      goto exit;
    }

    x = SLIST_NEXT(x, om_next);
  }

exit:
  os_mbuf_free_chain(om);

  return err;
}
