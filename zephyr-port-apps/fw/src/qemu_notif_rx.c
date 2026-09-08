/* SPDX-License-Identifier: Apache-2.0 */

// QEMU serial receive path for notifications (and other Pebble Protocol
// traffic) on the Zephyr port. Mirrors the reference chain
// qemu_serial -> comm_session -> Pebble Protocol -> blob_db, but compact and
// self-contained: a dedicated thread polls the SPP UART (uart1 = the
// pebble-tool channel, QEMU framing 0xFEED/proto/len/data/0xBEEF), parses a
// BlobDB insert into the Notifications DB, decodes the TimelineItem, and hands
// the notification to the display path on the KernelMain pump.

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>

#include <string.h>

#include "kernel/events.h"

void fw_pp_handle(const uint8_t *msg, uint16_t len);  // fw_pp_dispatch.c

#define SPP_UART DT_CHOSEN(pebble_spp)

#define QEMU_HDR_SIG 0xFEED
#define QEMU_FTR_SIG 0xBEEF
#define QEMU_PROTO_SPP 1
#define QEMU_PROTO_ANCS 0xf001
#define RX_STACK_SIZE 3072
#define RX_PRIORITY 6
#define MAX_MSG 2048

static const struct device *const s_spp = DEVICE_DT_GET(SPP_UART);
static K_THREAD_STACK_DEFINE(s_rx_stack, RX_STACK_SIZE);
static struct k_thread s_rx_thread;

static uint16_t prv_rd16be(const uint8_t *p) { return ((uint16_t)p[0] << 8) | p[1]; }

static void prv_rx_thread(void *a, void *b, void *c) {
  ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
  enum { S_SIG_MSB, S_SIG_LSB, S_HDR, S_DATA, S_FTR } st = S_SIG_MSB;
  static uint8_t msg[MAX_MSG];
  uint8_t hdrbuf[4];
  uint8_t hdr_got = 0;
  uint16_t proto = 0, dlen = 0, dgot = 0;
  uint8_t ftr_got = 0;

  while (true) {
    unsigned char ch;
    if (uart_poll_in(s_spp, &ch) != 0) {
      k_sleep(K_MSEC(5));
      continue;
    }
    switch (st) {
      case S_SIG_MSB:
        if (ch == 0xFE) { st = S_SIG_LSB; }
        break;
      case S_SIG_LSB:
        st = (ch == 0xED) ? S_HDR : S_SIG_MSB;
        hdr_got = 0;
        break;
      case S_HDR:
        hdrbuf[hdr_got++] = ch;
        if (hdr_got == 4) {
          proto = prv_rd16be(hdrbuf);
          dlen = prv_rd16be(hdrbuf + 2);
          dgot = 0;
          st = (dlen <= MAX_MSG) ? S_DATA : S_SIG_MSB;
          if (dlen == 0) { st = S_FTR; ftr_got = 0; }
        }
        break;
      case S_DATA:
        msg[dgot++] = ch;
        if (dgot == dlen) { st = S_FTR; ftr_got = 0; }
        break;
      case S_FTR:
        ftr_got++;
        if (ftr_got == 2) {
          if (proto == QEMU_PROTO_SPP) {
            fw_pp_handle(msg, dlen);
          } else if (proto == QEMU_PROTO_ANCS) {
            extern void fw_ancs_feed(const uint8_t *resp, size_t len);
            fw_ancs_feed(msg, dlen);
          }
          st = S_SIG_MSB;
        }
        break;
    }
  }
}

void fw_qemu_notif_rx_init(void) {
  if (!device_is_ready(s_spp)) {
    printk("NOTIF_RX spp uart not ready\n");
    return;
  }
  k_thread_create(&s_rx_thread, s_rx_stack, RX_STACK_SIZE, prv_rx_thread,
                  NULL, NULL, NULL, RX_PRIORITY, 0, K_NO_WAIT);
  printk("NOTIF_RX up\n");
}
