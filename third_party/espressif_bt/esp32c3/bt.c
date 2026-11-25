/*
 * SPDX-FileCopyrightText: 2015-2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2026 Core Devices LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// The OS adaptation of the ESP32-C3 Bluetooth controller library and its bring-up, from ESP-IDF
// 5.5.5 components/bt/controller/esp32c3/bt.c, for BLE only: no coexistence, no modem sleep, no
// power management, the controller library linked whole in flash (BT_CTRL_RUN_IN_FLASH_ONLY). The
// FreeRTOS calls of the wrappers map to the PebbleOS kernel; they keep FreeRTOS's results, 1 for
// success and 0 for failure, as the library expects.

#include "esp32c3_regs.h"
#include "esp_bt.h"
#include "phy.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pbl/drivers/mcu.h>
#include <pbl/drivers/rng.h>
#include <pbl/kernel/assert.h>
#include <pbl/kernel/fatal.h>
#include <pbl/kernel/idle.h>
#include <pbl/kernel/irq.h>
#include <pbl/kernel/malloc.h>
#include <pbl/kernel/mmio.h>
#include <pbl/kernel/msgq.h>
#include <pbl/kernel/mutex.h>
#include <pbl/kernel/sem.h>
#include <pbl/kernel/thread.h>
#include <pbl/kernel/time.h>
#include <pbl/logging/logging.h>
#include <pbl/soc/esp32c3/idle.h>

#include <devicetree/regs.h>
#include <esp32c3_bt.h>
#include <hardware/esp32c3_systimer.h>
#ifndef CONFIG_SOC_ESP32S3
#include <intmatrix.h>
#endif

PBL_LOG_MODULE_DEFINE(espressif_bt, CONFIG_ESP32C3_BT_LOG_LEVEL);

#define BTDM_MIN_SLEEP_DURATION  (24)
#define BTDM_MODEM_WAKE_UP_DELAY (8)

#define OSI_FUNCS_TIME_BLOCKING 0xffffffff
#define OSI_VERSION             0x0001000B
#define OSI_MAGIC_VALUE         0xFADEBEAD

// rtc_clk.h: fractional bits of the low power clock period
#define RTC_CLK_CAL_FRACT 19

#define BTDM_LPCLK_SEL_XTAL 0

#define MHZ 1000000

// FreeRTOS's pdPASS and pdFAIL
#define OSI_OK   1
#define OSI_FAIL 0

typedef void (*intr_handler_t)(void *arg);

/* OSI function */
struct osi_funcs_t {
  uint32_t _magic;
  uint32_t _version;
  int (*_interrupt_alloc)(int cpu_id, int source, intr_handler_t handler, void *arg,
                          void **ret_handle);
  int (*_interrupt_free)(void *handle);
  void (*_interrupt_handler_set_rsv)(int interrupt_no, intr_handler_t fn, void *arg);
  void (*_global_intr_disable)(void);
  void (*_global_intr_restore)(void);
  void (*_task_yield)(void);
  void (*_task_yield_from_isr)(void);
  void *(*_semphr_create)(uint32_t max, uint32_t init);
  void (*_semphr_delete)(void *semphr);
  int (*_semphr_take_from_isr)(void *semphr, void *hptw);
  int (*_semphr_give_from_isr)(void *semphr, void *hptw);
  int (*_semphr_take)(void *semphr, uint32_t block_time_ms);
  int (*_semphr_give)(void *semphr);
  void *(*_mutex_create)(void);
  void (*_mutex_delete)(void *mutex);
  int (*_mutex_lock)(void *mutex);
  int (*_mutex_unlock)(void *mutex);
  void *(*_queue_create)(uint32_t queue_len, uint32_t item_size);
  void (*_queue_delete)(void *queue);
  int (*_queue_send)(void *queue, void *item, uint32_t block_time_ms);
  int (*_queue_send_from_isr)(void *queue, void *item, void *hptw);
  int (*_queue_recv)(void *queue, void *item, uint32_t block_time_ms);
  int (*_queue_recv_from_isr)(void *queue, void *item, void *hptw);
  int (*_task_create)(void *task_func, const char *name, uint32_t stack_depth, void *param,
                      uint32_t prio, void *task_handle, uint32_t core_id);
  void (*_task_delete)(void *task_handle);
  bool (*_is_in_isr)(void);
  int (*_cause_sw_intr_to_core)(int core_id, int intr_no);
  void *(*_malloc)(size_t size);
  void *(*_malloc_internal)(size_t size);
  void (*_free)(void *p);
  int (*_read_efuse_mac)(uint8_t mac[6]);
  void (*_srand)(unsigned int seed);
  int (*_rand)(void);
  uint32_t (*_btdm_lpcycles_2_hus)(uint32_t cycles, uint32_t *error_corr);
  uint32_t (*_btdm_hus_2_lpcycles)(uint32_t hus);
  bool (*_btdm_sleep_check_duration)(int32_t *slot_cnt);
  void (*_btdm_sleep_enter_phase1)(uint32_t lpcycles); /* called when interrupt is disabled */
  void (*_btdm_sleep_enter_phase2)(void);
  void (*_btdm_sleep_exit_phase1)(void); /* called from ISR */
  void (*_btdm_sleep_exit_phase2)(void); /* called from ISR */
  void (*_btdm_sleep_exit_phase3)(void); /* called from task */
  void (*_coex_wifi_sleep_set)(bool sleep);
  int (*_coex_core_ble_conn_dyn_prio_get)(bool *low, bool *high);
  int (*_coex_schm_register_btdm_callback)(void *callback);
  void (*_coex_schm_status_bit_set)(uint32_t type, uint32_t status);
  void (*_coex_schm_status_bit_clear)(uint32_t type, uint32_t status);
  uint32_t (*_coex_schm_interval_get)(void);
  uint8_t (*_coex_schm_curr_period_get)(void);
  void *(*_coex_schm_curr_phase_get)(void);
  int (*_interrupt_enable)(void *handle);
  int (*_interrupt_disable)(void *handle);
  void (*_esp_hw_power_down)(void);
  void (*_esp_hw_power_up)(void);
  void (*_ets_backup_dma_copy)(uint32_t reg, uint32_t mem_addr, uint32_t num, bool to_rem);
  void *(*_malloc_retention)(size_t size);
  void (*_ets_delay_us)(uint32_t us);
  void (*_btdm_rom_table_ready)(void);
  bool (*_coex_bt_wakeup_request)(void);
  void (*_coex_bt_wakeup_request_end)(void);
  int64_t (*_get_time_us)(void);
  void (*_assert)(void);
};

// 2 words, 61 pointers
static_assert(sizeof(struct osi_funcs_t) == 63 * 4, "osi_funcs_t does not match the library");
static_assert(offsetof(struct osi_funcs_t, _assert) == 62 * 4, "osi_funcs_t field order");
static_assert(sizeof(esp_bt_controller_config_t) == 72, "esp_bt_controller_config_t size");

/* OSI */
extern int btdm_osi_funcs_register(void *osi_funcs);
/* Initialise and De-initialise */
extern int btdm_controller_init(esp_bt_controller_config_t *config_opts);
extern int btdm_controller_enable(uint8_t mode);
extern bool btdm_power_state_active(void);
extern void btdm_controller_disable(void);
extern uint8_t btdm_controller_get_mode(void);
extern const char *btdm_controller_get_compile_version(void);
extern void btdm_controller_rom_data_init(void);
/* Low Power Clock */
extern bool btdm_lpclk_select_src(uint32_t sel);
extern bool btdm_lpclk_set_div(uint32_t div);
/* VHCI */
extern bool API_vhci_host_check_send_available(void);
extern void API_vhci_host_send_packet(uint8_t *data, uint16_t len);
extern int API_vhci_host_register_callback(const vhci_host_callback_t *callback);

extern void coex_pti_v2(void);
extern void sdk_config_extend_set_pll_track(bool enable);

extern void scan_stack_enableAdvFlowCtrlVsCmd(bool en);
extern void adv_stack_enableClearLegacyAdvVsCmd(bool en);
extern void advFilter_stack_enableDupExcListVsCmd(bool en);
extern void chanSel_stack_enableSetCsaVsCmd(bool en);

// ROM: ets_delay_us(), calibrated by the SoC init
extern void ets_delay_us(uint32_t us);

/* Local variable definition
 ***************************************************************************
 */

// The controller library allocates three interrupts, BT_BB, RWBT and RWBLE
#define INTR_SLOTS 4

// Timing statistics, for btc status: ISR handler duration, ISR signal to thread wake-up
static int64_t get_time_us_wrapper(void);
static volatile int64_t s_isr_signal_us;
static uint32_t s_isr_max_us, s_wake_max_us;
static void prv_woken(void);

// The source first: the handle the library gets points at it
static struct btdm_intr {
  pbl_irq_t source;
  intr_handler_t fn;
  void *arg;
  uint32_t count;
} s_intr[INTR_SLOTS];
static uint32_t s_intr_count;

static void prv_intr_trampoline(void *arg) {
  struct btdm_intr *intr = arg;

  const int64_t start = get_time_us_wrapper();

  intr->count++;
  intr->fn(intr->arg);
  const uint32_t d = (uint32_t)(get_time_us_wrapper() - start);
  if (d > s_isr_max_us) {
    s_isr_max_us = d;
  }
}

#ifdef CONFIG_SOC_ESP32S3
static void prv_radio_irq(pbl_irq_t source) {
  for (uint32_t i = 0; i < s_intr_count; i++) {
    if (s_intr[i].source == source) {
      prv_intr_trampoline(&s_intr[i]);
      return;
    }
  }
}
PBL_IRQ_CONNECT(BT_BB, 1, prv_radio_irq, PBL_SOC_IRQN_BT_BB, 0);
PBL_IRQ_CONNECT(RWBT, 1, prv_radio_irq, PBL_SOC_IRQN_RWBT, 0);
PBL_IRQ_CONNECT(RWBLE, 1, prv_radio_irq, PBL_SOC_IRQN_RWBLE, 0);
#endif

bool esp32c3_bt_intr_stats(uint32_t i, uint32_t *source, uint32_t *count) {
  if (i >= s_intr_count) {
    return false;
  }
  *source = s_intr[i].source;
  *count = s_intr[i].count;
  return true;
}

// low power clock period in micro seconds, and its fractional bits: the main crystal, divided to
// 1 MHz
static uint32_t btdm_lpcycle_us;
static uint8_t btdm_lpcycle_us_frac;

static enum {
  BTDM_STATUS_IDLE,
  BTDM_STATUS_INITED,
  BTDM_STATUS_ENABLED,
} btdm_controller_status;

static struct osi_funcs_t *osi_funcs_p;

/* Interrupts */

static int interrupt_alloc_wrapper(int cpu_id, int source, intr_handler_t handler, void *arg,
                                   void **ret_handle) {
  int ret;

  if (s_intr_count == INTR_SLOTS) {
    return -ENOSPC;
  }

  struct btdm_intr *intr = &s_intr[s_intr_count];

  intr->source = (pbl_irq_t)source;
  intr->fn = handler;
  intr->arg = arg;
#ifdef CONFIG_SOC_ESP32S3
  ret =
      (source == PBL_SOC_IRQN_BT_BB || source == PBL_SOC_IRQN_RWBT || source == PBL_SOC_IRQN_RWBLE)
          ? 0
          : -EINVAL;
#else
  ret = esp32c3_irq_attach((pbl_irq_t)source, prv_intr_trampoline, intr);
#endif
  PBL_LOG_DBG("interrupt source %d: %d", source, ret);
  if (ret != 0) {
    return ret;
  }

  // ESP-IDF's esp_intr_alloc() enables it at once
  s_intr_count++;
  *ret_handle = &intr->source;
  pbl_irq_enable((pbl_irq_t)source);

  return 0;
}

static int interrupt_free_wrapper(void *handle) {
#ifdef CONFIG_SOC_ESP32S3
  pbl_irq_disable(*(pbl_irq_t *)handle);
#else
  esp32c3_irq_detach(*(pbl_irq_t *)handle);
#endif
  return 0;
}

static int interrupt_enable_wrapper(void *handle) {
  pbl_irq_enable(*(pbl_irq_t *)handle);
  return 0;
}

static int interrupt_disable_wrapper(void *handle) {
  pbl_irq_disable(*(pbl_irq_t *)handle);
  return 0;
}

static void global_interrupt_disable(void) {
  pbl_irq_lock();
}

static void global_interrupt_restore(void) {
  pbl_irq_unlock();
}

// A switch an ISR asks for happens as it returns
static void task_yield_from_isr(void) {
}

/* Semaphores, mutexes, queues */

static pbl_timeout_t prv_timeout(uint32_t block_time_ms) {
  return block_time_ms == OSI_FUNCS_TIME_BLOCKING ? PBL_FOREVER : PBL_MSEC(block_time_ms);
}

static void *semphr_create_wrapper(uint32_t max, uint32_t init) {
  struct pbl_sem *semphr = pbl_malloc(sizeof(*semphr));

  PBL_ASSERTN(semphr != nullptr);
  pbl_sem_init(semphr, init, max);

  return semphr;
}

static void semphr_delete_wrapper(void *semphr) {
  if (semphr == nullptr) {
    return;
  }
  pbl_sem_deinit(semphr);
  pbl_free(semphr);
}

static int semphr_take_from_isr_wrapper(void *semphr, void *hptw) {
  if (hptw != nullptr) {
    *(int *)hptw = 0;
  }
  return pbl_sem_take(semphr, PBL_NO_WAIT) == 0 ? OSI_OK : OSI_FAIL;
}

// FreeRTOS fails a give beyond the limit, the kernel drops it
static int prv_sem_give(struct pbl_sem *s) {
  int ret = OSI_FAIL;

  pbl_irq_lock();
  if (pbl_sem_count(s) < s->limit) {
    pbl_sem_give(s);
    ret = OSI_OK;
  }
  pbl_irq_unlock();

  return ret;
}

static int semphr_give_from_isr_wrapper(void *semphr, void *hptw) {
  if (hptw != nullptr) {
    *(int *)hptw = 0;
  }
  s_isr_signal_us = get_time_us_wrapper();
  return prv_sem_give(semphr);
}

static int semphr_take_wrapper(void *semphr, uint32_t block_time_ms) {
  const int ret = pbl_sem_take(semphr, prv_timeout(block_time_ms)) == 0 ? OSI_OK : OSI_FAIL;

  prv_woken();
  return ret;
}

static int semphr_give_wrapper(void *semphr) {
  return prv_sem_give(semphr);
}

static void *mutex_create_wrapper(void) {
  struct pbl_mutex *mutex = pbl_malloc(sizeof(*mutex));

  PBL_ASSERTN(mutex != nullptr);
  pbl_mutex_init(mutex);

  return mutex;
}

static void mutex_delete_wrapper(void *mutex) {
  pbl_mutex_deinit(mutex);
  pbl_free(mutex);
}

static int mutex_lock_wrapper(void *mutex) {
  return pbl_mutex_lock(mutex, PBL_FOREVER) == 0 ? OSI_OK : OSI_FAIL;
}

static int mutex_unlock_wrapper(void *mutex) {
  pbl_mutex_unlock(mutex);
  return OSI_OK;
}

void esp32c3_bt_timing_stats(uint32_t *isr_max_us, uint32_t *wake_max_us) {
  *isr_max_us = s_isr_max_us;
  *wake_max_us = s_wake_max_us;
  s_isr_max_us = 0U;
  s_wake_max_us = 0U;
}

static void prv_woken(void) {
  const int64_t sent = s_isr_signal_us;

  if (sent != 0) {
    const uint32_t d = (uint32_t)(get_time_us_wrapper() - sent);
    s_isr_signal_us = 0;
    if (d > s_wake_max_us) {
      s_wake_max_us = d;
    }
  }
}

struct btdm_queue {
  struct pbl_msgq q;
  uint8_t buf[];
};

static void *queue_create_wrapper(uint32_t queue_len, uint32_t item_size) {
  struct btdm_queue *queue = pbl_malloc(sizeof(*queue) + queue_len * item_size);

  PBL_ASSERTN(queue != nullptr);
  pbl_msgq_init(&queue->q, queue->buf, item_size, queue_len);

  return queue;
}

static void queue_delete_wrapper(void *queue) {
  struct btdm_queue *queue_item = queue;

  if (queue_item != nullptr) {
    pbl_msgq_deinit(&queue_item->q);
    pbl_free(queue_item);
  }
}

static int queue_send_wrapper(void *queue, void *item, uint32_t block_time_ms) {
  return pbl_msgq_put(&((struct btdm_queue *)queue)->q, item, prv_timeout(block_time_ms)) == 0
             ? OSI_OK
             : OSI_FAIL;
}

static int queue_send_from_isr_wrapper(void *queue, void *item, void *hptw) {
  if (hptw != nullptr) {
    *(int *)hptw = 0;
  }
  s_isr_signal_us = get_time_us_wrapper();
  return pbl_msgq_put(&((struct btdm_queue *)queue)->q, item, PBL_NO_WAIT) == 0 ? OSI_OK : OSI_FAIL;
}

static int queue_recv_wrapper(void *queue, void *item, uint32_t block_time_ms) {
  const int ret =
      pbl_msgq_get(&((struct btdm_queue *)queue)->q, item, prv_timeout(block_time_ms)) == 0
          ? OSI_OK
          : OSI_FAIL;

  prv_woken();
  return ret;
}

static int queue_recv_from_isr_wrapper(void *queue, void *item, void *hptw) {
  if (hptw != nullptr) {
    *(int *)hptw = 0;
  }
  return pbl_msgq_get(&((struct btdm_queue *)queue)->q, item, PBL_NO_WAIT) == 0 ? OSI_OK : OSI_FAIL;
}

/* Task: the library creates one, the controller's */

static struct pbl_thread s_ctrl_thread;
PBL_THREAD_STACK_DEFINE(s_ctrl_stack, CONFIG_ESP32C3_BT_CONTROLLER_STACK_SIZE);
static bool s_ctrl_thread_used;

static int task_create_wrapper(void *task_func, const char *name, uint32_t stack_depth, void *param,
                               uint32_t prio, void *task_handle, uint32_t core_id) {
  PBL_LOG_DBG("task %s: %lu bytes, priority %lu", name, (unsigned long)stack_depth,
              (unsigned long)prio);
  if (s_ctrl_thread_used || stack_depth > CONFIG_ESP32C3_BT_CONTROLLER_STACK_SIZE) {
    PBL_LOG_ERR("cannot create task %s", name);
    return OSI_FAIL;
  }

  const struct pbl_thread_attr attr = {
    .name = name,
    .entry = (pbl_thread_entry_t)task_func,
    .arg = param,
    .prio = CONFIG_ESP32C3_BT_CONTROLLER_PRIO,
    .privileged = true,
    .stack = s_ctrl_stack,
    .stack_size = sizeof(s_ctrl_stack),
  };

  s_ctrl_thread_used = true;
  (void)pbl_thread_create(&s_ctrl_thread, &attr);
  if (task_handle != nullptr) {
    *(void **)task_handle = &s_ctrl_thread;
  }

  return OSI_OK;
}

static void task_delete_wrapper(void *task_handle) {
  pbl_thread_abort(task_handle);
}

static bool is_in_isr_wrapper(void) {
  return pbl_in_isr();
}

/* Memory: all of SRAM is internal and DMA-capable */

static void *malloc_wrapper(size_t size) {
  void *p = pbl_malloc(size);

  if (p == nullptr) {
    PBL_LOG_ERR("Malloc failed");
  }
  return p;
}

static void free_wrapper(void *p) {
  pbl_free(p);
}

/* Identity and randomness */

// esp_read_mac(ESP_MAC_BT): the factory MAC plus 2
static int read_mac_wrapper(uint8_t mac[6]) {
  size_t len = 6;
  int ret = mcu_get_serial(mac, &len);

  if (ret != 0 || len != 6) {
    return -EIO;
  }
  mac[5] += 2;
  PBL_LOG_DBG("Bluetooth MAC: %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3],
              mac[4], mac[5]);

  return 0;
}

static void srand_wrapper(unsigned int seed) {
  /* empty function */
}

static int rand_wrapper(void) {
  uint32_t r = 0;

  (void)rng_rand(&r);
  return (int)r;
}

/* Low power clock and sleep: modem sleep is off */

static uint32_t btdm_lpcycles_2_hus(uint32_t cycles, uint32_t *error_corr) {
  uint64_t local_error_corr = (error_corr == NULL) ? 0 : (uint64_t)(*error_corr);
  uint64_t res = (uint64_t)btdm_lpcycle_us * cycles * 2;
  local_error_corr += res;
  res = (local_error_corr >> btdm_lpcycle_us_frac);
  local_error_corr -= (res << btdm_lpcycle_us_frac);
  if (error_corr) {
    *error_corr = (uint32_t)local_error_corr;
  }
  return (uint32_t)res;
}

static uint32_t btdm_hus_2_lpcycles(uint32_t hus) {
  uint64_t cycles = ((uint64_t)(hus) << btdm_lpcycle_us_frac) / btdm_lpcycle_us;
  cycles >>= 1;

  return (uint32_t)cycles;
}

static bool btdm_sleep_check_duration(int32_t *half_slot_cnt) {
  if (*half_slot_cnt < BTDM_MIN_SLEEP_DURATION) {
    return false;
  }
  /* wake up in advance considering the delay in enabling PHY/RF */
  *half_slot_cnt -= BTDM_MODEM_WAKE_UP_DELAY;
  return true;
}

static void btdm_sleep_enter_phase1_wrapper(uint32_t lpcycles) {
}

static void btdm_sleep_enter_phase2_wrapper(void) {
}

static void btdm_sleep_exit_phase3_wrapper(void) {
}

/* Coexistence: none */

static void coex_wifi_sleep_set_hook(bool sleep) {
}

static int coex_schm_register_btdm_callback_wrapper(void *callback) {
  return 0;
}

static void coex_schm_status_bit_clear_wrapper(uint32_t type, uint32_t status) {
}

static void coex_schm_status_bit_set_wrapper(uint32_t type, uint32_t status) {
}

static uint32_t coex_schm_interval_get_wrapper(void) {
  return 0;
}

static uint8_t coex_schm_curr_period_get_wrapper(void) {
  return 1;
}

static void *coex_schm_curr_phase_get_wrapper(void) {
  return NULL;
}

static bool coex_bt_wakeup_request(void) {
  return false;
}

static void coex_bt_wakeup_request_end(void) {
}

/* Power, retention: no MAC/BB power down */

static void btdm_hw_mac_power_down_wrapper(void) {
}

static void btdm_hw_mac_power_up_wrapper(void) {
}

static void btdm_backup_dma_copy_wrapper(uint32_t reg, uint32_t mem_addr, uint32_t num,
                                         bool to_mem) {
}

#ifndef CONFIG_ESP32C3_BT_CONTROLLER_IN_FLASH
extern void ble_scan_funcs_reset(void);
extern void ble_42_adv_funcs_reset(void);
extern void ble_init_funcs_reset(void);
extern void ble_con_funcs_reset(void);
extern void ble_base_funcs_reset(void);
extern void ble_enc_funcs_reset(void);
extern void ble_dtm_funcs_reset(void);
#endif

// The controller in ROM, patched: point its function tables at the features configured. The
// library linked whole in flash has none.
static void btdm_funcs_table_ready_wrapper(void) {
#ifndef CONFIG_ESP32C3_BT_CONTROLLER_IN_FLASH
  ble_base_funcs_reset();
  ble_42_adv_funcs_reset();
  ble_dtm_funcs_reset();
  ble_scan_funcs_reset();
  ble_enc_funcs_reset();
  ble_init_funcs_reset();
  ble_con_funcs_reset();
#endif
}

// esp_timer_get_time(): microseconds from SYSTIMER unit 0, which counts at 16 MHz. The snapshot
// is taken under the lock, as the kernel's timer driver takes its own.
static int64_t get_time_us_wrapper(void) {
  uint64_t now;

  pbl_irq_lock();
  pbl_writel(SYSTIMER_UNIT_OP_UPDATE,
             PBL_DT_REG_ESP_ESP32C3_SYSTIMER + ESP32C3_SYSTIMER_UNIT0_OP_OFFSET);
  while ((pbl_readl(PBL_DT_REG_ESP_ESP32C3_SYSTIMER + ESP32C3_SYSTIMER_UNIT0_OP_OFFSET) &
          SYSTIMER_UNIT_OP_VALUE_VALID) == 0U) {
  }
  now = ((uint64_t)(pbl_readl(PBL_DT_REG_ESP_ESP32C3_SYSTIMER +
                              ESP32C3_SYSTIMER_UNIT0_VALUE_HI_OFFSET) &
                    SYSTIMER_VALUE_HI_MASK)
         << 32) |
        pbl_readl(PBL_DT_REG_ESP_ESP32C3_SYSTIMER + ESP32C3_SYSTIMER_UNIT0_VALUE_LO_OFFSET);
  pbl_irq_unlock();

  return (int64_t)(now / 16U);
}

static int64_t prv_sample_cpu_clock(uint32_t *cycles) {
  pbl_irq_lock();
  int64_t us = get_time_us_wrapper();
  *cycles = pbl_cycle_get_32();
  pbl_irq_unlock();
  return us;
}

uint32_t esp_bt_measured_cpu_mhz(void) {
  uint32_t start_cycles, end_cycles;
  int64_t start_us = prv_sample_cpu_clock(&start_cycles);
  int64_t elapsed;
  do {
    elapsed = prv_sample_cpu_clock(&end_cycles) - start_us;
  } while (elapsed < 1000);
  return (uint32_t)((end_cycles - start_cycles + elapsed / 2) / elapsed);
}

static void assert_wrapper(void) {
  PBL_LOG_ERR("controller assert");
}

static const struct osi_funcs_t osi_funcs_ro = {
  ._magic = OSI_MAGIC_VALUE,
  ._version = OSI_VERSION,
  ._interrupt_alloc = interrupt_alloc_wrapper,
  ._interrupt_free = interrupt_free_wrapper,
  ._interrupt_handler_set_rsv = NULL,
  ._global_intr_disable = global_interrupt_disable,
  ._global_intr_restore = global_interrupt_restore,
  ._task_yield = pbl_thread_yield,
  ._task_yield_from_isr = task_yield_from_isr,
  ._semphr_create = semphr_create_wrapper,
  ._semphr_delete = semphr_delete_wrapper,
  ._semphr_take_from_isr = semphr_take_from_isr_wrapper,
  ._semphr_give_from_isr = semphr_give_from_isr_wrapper,
  ._semphr_take = semphr_take_wrapper,
  ._semphr_give = semphr_give_wrapper,
  ._mutex_create = mutex_create_wrapper,
  ._mutex_delete = mutex_delete_wrapper,
  ._mutex_lock = mutex_lock_wrapper,
  ._mutex_unlock = mutex_unlock_wrapper,
  ._queue_create = queue_create_wrapper,
  ._queue_delete = queue_delete_wrapper,
  ._queue_send = queue_send_wrapper,
  ._queue_send_from_isr = queue_send_from_isr_wrapper,
  ._queue_recv = queue_recv_wrapper,
  ._queue_recv_from_isr = queue_recv_from_isr_wrapper,
  ._task_create = task_create_wrapper,
  ._task_delete = task_delete_wrapper,
  ._is_in_isr = is_in_isr_wrapper,
  ._cause_sw_intr_to_core = NULL,
  ._malloc = malloc_wrapper,
  ._malloc_internal = malloc_wrapper,
  ._malloc_retention = malloc_wrapper,
  ._free = free_wrapper,
  ._read_efuse_mac = read_mac_wrapper,
  ._srand = srand_wrapper,
  ._rand = rand_wrapper,
  ._btdm_lpcycles_2_hus = btdm_lpcycles_2_hus,
  ._btdm_hus_2_lpcycles = btdm_hus_2_lpcycles,
  ._btdm_sleep_check_duration = btdm_sleep_check_duration,
  ._btdm_sleep_enter_phase1 = btdm_sleep_enter_phase1_wrapper,
  ._btdm_sleep_enter_phase2 = btdm_sleep_enter_phase2_wrapper,
  ._btdm_sleep_exit_phase1 = NULL,
  ._btdm_sleep_exit_phase2 = NULL,
  ._btdm_sleep_exit_phase3 = btdm_sleep_exit_phase3_wrapper,
  ._coex_wifi_sleep_set = coex_wifi_sleep_set_hook,
  ._coex_core_ble_conn_dyn_prio_get = NULL,
  ._coex_schm_register_btdm_callback = coex_schm_register_btdm_callback_wrapper,
  ._coex_schm_status_bit_set = coex_schm_status_bit_set_wrapper,
  ._coex_schm_status_bit_clear = coex_schm_status_bit_clear_wrapper,
  ._coex_schm_interval_get = coex_schm_interval_get_wrapper,
  ._coex_schm_curr_period_get = coex_schm_curr_period_get_wrapper,
  ._coex_schm_curr_phase_get = coex_schm_curr_phase_get_wrapper,
  ._interrupt_enable = interrupt_enable_wrapper,
  ._interrupt_disable = interrupt_disable_wrapper,
  ._esp_hw_power_down = btdm_hw_mac_power_down_wrapper,
  ._esp_hw_power_up = btdm_hw_mac_power_up_wrapper,
  ._ets_backup_dma_copy = btdm_backup_dma_copy_wrapper,
  ._ets_delay_us = ets_delay_us,
  ._btdm_rom_table_ready = btdm_funcs_table_ready_wrapper,
  ._coex_bt_wakeup_request = coex_bt_wakeup_request,
  ._coex_bt_wakeup_request_end = coex_bt_wakeup_request_end,
  ._get_time_us = get_time_us_wrapper,
  ._assert = assert_wrapper,
};

/* Clocks, resets and power */

static inline void esp_bt_power_domain_on(void) {
  // Bluetooth module power up
  pbl_modifyl(RTC_CNTL_BT_FORCE_PD, 0, RTC_CNTL_DIG_PWC_REG);
  pbl_modifyl(RTC_CNTL_BT_FORCE_ISO, 0, RTC_CNTL_DIG_ISO_REG);
  esp_wifi_bt_power_domain_on();
}

static int btdm_low_power_mode_init(esp_bt_controller_config_t *cfg) {
  bool select_src_ret;
  bool set_div_ret;

  // Modem sleep off: the low power clock is the main crystal, which stays on
  esp_bt_init_stage(EspBtStage_LpClock);
  select_src_ret = btdm_lpclk_select_src(BTDM_LPCLK_SEL_XTAL);
  esp_bt_init_stage(EspBtStage_LpDiv);
  set_div_ret = btdm_lpclk_set_div(ESP32C3_XTAL_HZ / MHZ);
  if (!select_src_ret || !set_div_ret) {
    return -EIO;
  }
  btdm_lpcycle_us_frac = RTC_CLK_CAL_FRACT;
  btdm_lpcycle_us = 1 << (btdm_lpcycle_us_frac);

  return 0;
}

static void prv_light_sleep_set_blocked(bool block);

int esp32c3_bt_controller_init(void) {
  esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  int err;

  if (btdm_controller_status == BTDM_STATUS_INITED) {
    return 0;
  }
  if (btdm_controller_status != BTDM_STATUS_IDLE) {
    return -EALREADY;
  }

  esp_bt_init_stage(EspBtStage_Power);
  prv_light_sleep_set_blocked(true);
  esp_bt_power_domain_on();

  esp_bt_init_stage(EspBtStage_Rom);
  btdm_controller_rom_data_init();
#ifdef CONFIG_ESP32C3_BT_LOG_LEVEL_DEBUG
  // The controller prints its asserts' file and line through ets_printf() above 0
  extern int g_bt_plf_log_level;
  g_bt_plf_log_level = 1;
#endif

  osi_funcs_p = pbl_malloc(sizeof(struct osi_funcs_t));
  if (osi_funcs_p == nullptr) {
    return -ENOMEM;
  }

  memcpy(osi_funcs_p, &osi_funcs_ro, sizeof(struct osi_funcs_t));
  esp_bt_init_stage(EspBtStage_Osi);
  if (btdm_osi_funcs_register(osi_funcs_p) != 0) {
    return -EINVAL;
  }

  PBL_LOG_INFO("BT controller compile version [%s]", btdm_controller_get_compile_version());

  PBL_LOG_DBG("Enabling Bluetooth clocks and releasing controller reset");
  esp_bt_init_stage(EspBtStage_Clocks);
  periph_bt_module_enable();
  esp_bt_init_stage(EspBtStage_Reset);
  periph_bt_module_reset();

  PBL_LOG_DBG("Selecting XTAL controller clock");
  err = btdm_low_power_mode_init(&cfg);
  if (err != 0) {
    PBL_LOG_ERR("Low power module initialization failed");
    return err;
  }

  PBL_LOG_DBG("Initializing link-layer controller");
  esp_bt_init_stage(EspBtStage_ControllerInit);
  err = btdm_controller_init(&cfg);
  if (err != 0) {
    PBL_LOG_ERR("btdm_controller_init %d", err);
    return -ENOMEM;
  }

  PBL_LOG_DBG("Configuring HCI vendor commands");
  esp_bt_init_stage(EspBtStage_Features);
  scan_stack_enableAdvFlowCtrlVsCmd(true);
  adv_stack_enableClearLegacyAdvVsCmd(true);
  advFilter_stack_enableDupExcListVsCmd(true);
  chanSel_stack_enableSetCsaVsCmd(true);

  btdm_controller_status = BTDM_STATUS_INITED;

  return 0;
}

static void prv_light_sleep_set_blocked(bool block) {
#ifdef CONFIG_SOC_ESP32C3_LIGHT_SLEEP
  static bool s_blocked;

  if (block == s_blocked) {
    return;
  }
  if (block) {
    pbl_idle_state_block(SOC_ESP32C3_IDLE_LIGHT_SLEEP);
  } else {
    pbl_idle_state_release(SOC_ESP32C3_IDLE_LIGHT_SLEEP);
  }
  s_blocked = block;
#else
  (void)block;
#endif
}

int esp32c3_bt_controller_enable(void) {
  if (btdm_controller_status != BTDM_STATUS_INITED) {
    return -EINVAL;
  }

  if (btdm_controller_get_mode() != ESP_BT_MODE_BLE) {
    PBL_LOG_ERR("controller mode %d", btdm_controller_get_mode());
    return -EINVAL;
  }

  // Modem sleep is off (XTAL LP clock): stopping digital clocks in light sleep breaks active BLE
  prv_light_sleep_set_blocked(true);

  /* Notice the init order: esp_phy_enable() -> bt_bb_v2_init_cmplx() -> coex_pti_v2() */
  PBL_LOG_DBG("Enabling radio PHY");
  esp_bt_init_stage(EspBtStage_Phy);
  esp_phy_enable(PHY_MODEM_BT);

  // Disable pll track by default in BLE controller on ESP32-C3 and ESP32-S3
  sdk_config_extend_set_pll_track(false);

  esp_bt_init_stage(EspBtStage_Enable);
  if (btdm_controller_enable(ESP_BT_MODE_BLE) != 0) {
    esp_phy_disable(PHY_MODEM_BT);
    return -EIO;
  }

  esp_bt_init_stage(EspBtStage_Coex);
  coex_pti_v2();

  btdm_controller_status = BTDM_STATUS_ENABLED;

  return 0;
}

int esp32c3_bt_controller_disable(void) {
  if (btdm_controller_status != BTDM_STATUS_ENABLED) {
    return 0;
  }
  // Modem sleep is disabled. Only shut down a controller that is awake.
  if (!btdm_power_state_active()) {
    return -EBUSY;
  }
  btdm_controller_disable();
  esp_phy_disable(PHY_MODEM_BT);
  btdm_controller_status = BTDM_STATUS_INITED;
  prv_light_sleep_set_blocked(false);
  return 0;
}

bool esp32c3_bt_controller_enabled(void) {
  return btdm_controller_status == BTDM_STATUS_ENABLED;
}

const char *esp32c3_bt_controller_version(void) {
  return btdm_controller_get_compile_version();
}

bool esp32c3_bt_vhci_send_available(void) {
  if (btdm_controller_status != BTDM_STATUS_ENABLED) {
    return false;
  }
  return API_vhci_host_check_send_available();
}

void esp32c3_bt_vhci_send(uint8_t *data, uint16_t len) {
  if (btdm_controller_status != BTDM_STATUS_ENABLED) {
    return;
  }
  API_vhci_host_send_packet(data, len);
}

int esp32c3_bt_vhci_register(const struct esp32c3_bt_vhci_callbacks *cb) {
  static vhci_host_callback_t s_cb;

  if (btdm_controller_status != BTDM_STATUS_ENABLED) {
    return -EINVAL;
  }
  // One host only: the NimBLE transport or the shell, whichever came first
  if (s_cb.notify_host_recv != NULL && s_cb.notify_host_recv != cb->recv) {
    return -EBUSY;
  }
  s_cb.notify_host_send_available = cb->send_available;
  s_cb.notify_host_recv = cb->recv;
  return API_vhci_host_register_callback(&s_cb) == 0 ? 0 : -EIO;
}
