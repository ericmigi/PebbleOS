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

#define SPP_UART DT_CHOSEN(pebble_spp)

#define QEMU_HDR_SIG 0xFEED
#define QEMU_FTR_SIG 0xBEEF
#define QEMU_PROTO_SPP 1
#define QEMU_PROTO_ANCS 0xf001
#define BLOB_DB_ENDPOINT 0xb1db
#define BLOB_DB_CMD_INSERT 0x01
#define BLOB_DB_CMD_INSERT_TS 0x0D
#define BLOB_DB_ID_NOTIFS 0x04

#define ATTR_TITLE 1
#define ATTR_SUBTITLE 2
#define ATTR_BODY 3
#define ATTR_ICON 4

#define RX_STACK_SIZE 3072
#define RX_PRIORITY 6
#define MAX_MSG 2048

static const struct device *const s_spp = DEVICE_DT_GET(SPP_UART);
static K_THREAD_STACK_DEFINE(s_rx_stack, RX_STACK_SIZE);
static struct k_thread s_rx_thread;

// Display entry (qemu_notif_display.c); weak so the transport can land before
// the UI does.
void fw_notification_show(const char *title, const char *subtitle, const char *body,
                          uint32_t icon);
__attribute__((weak)) void fw_notification_show(const char *title, const char *subtitle,
                                                const char *body, uint32_t icon) {
  (void)icon;
  printk("NOTIF_RX title=\"%s\" subtitle=\"%s\" body=\"%s\"\n",
         title ? title : "", subtitle ? subtitle : "", body ? body : "");
}

static uint16_t prv_rd16be(const uint8_t *p) { return ((uint16_t)p[0] << 8) | p[1]; }
static uint16_t prv_rd16le(const uint8_t *p) { return ((uint16_t)p[1] << 8) | p[0]; }
static uint32_t prv_rd32le(const uint8_t *p) {
  return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
}

// Parse a raw Pebble Protocol payload (already de-framed): [len:2 BE][ep:2 BE][data].
static void prv_handle_pp(const uint8_t *msg, uint16_t len) {
  if (len < 4) {
    return;
  }
  const uint16_t pp_len = prv_rd16be(msg);
  const uint16_t endpoint = prv_rd16be(msg + 2);
  const uint8_t *data = msg + 4;
  if (4 + pp_len > len || endpoint != BLOB_DB_ENDPOINT) {
    return;
  }
  // BlobDB: [cmd:1][token:2][db_id:1][key_len:1][key:N][val_len:2][value:M]
  if (pp_len < 7) {
    return;
  }
  const uint8_t cmd = data[0];
  if (cmd != BLOB_DB_CMD_INSERT && cmd != BLOB_DB_CMD_INSERT_TS) {
    return;
  }
  const uint8_t db_id = data[3];
  if (db_id != BLOB_DB_ID_NOTIFS) {
    return;
  }
  const uint8_t key_len = data[4];
  const uint8_t *iter = data + 5 + key_len;
  const uint8_t *end = data + pp_len;
  if (iter + 2 > end) {
    return;
  }
  const uint16_t val_len = prv_rd16le(iter);
  iter += 2;
  const uint8_t *value = iter;
  if (value + val_len > end) {
    return;
  }
  // TimelineItem (LE): uuid16 + parent16 + ts4 + dur2 + type1 + flags2 + layout1
  //                    + data_len2 + attr_count1 + action_count1 + attributes...
  const size_t hdr = 16 + 16 + 4 + 2 + 1 + 2 + 1;
  if (val_len < hdr + 4) {
    return;
  }
  const uint8_t attr_count = value[hdr + 2];
  const uint8_t *a = value + hdr + 4;
  const uint8_t *vend = value + val_len;
  static char title[64], subtitle[64], body[128];
  title[0] = subtitle[0] = body[0] = '\0';
  uint32_t icon = 0;
  for (uint8_t i = 0; i < attr_count && a + 3 <= vend; ++i) {
    const uint8_t id = a[0];
    const uint16_t alen = prv_rd16le(a + 1);
    const uint8_t *content = a + 3;
    if (content + alen > vend) {
      break;
    }
    char *dst = NULL;
    size_t cap = 0;
    if (id == ATTR_TITLE) { dst = title; cap = sizeof(title); }
    else if (id == ATTR_SUBTITLE) { dst = subtitle; cap = sizeof(subtitle); }
    else if (id == ATTR_BODY) { dst = body; cap = sizeof(body); }
    else if (id == ATTR_ICON && alen == 4) { icon = prv_rd32le(content); }
    if (dst) {
      const size_t n = (alen < cap - 1) ? alen : cap - 1;
      memcpy(dst, content, n);
      dst[n] = '\0';
    }
    a = content + alen;
  }
  fw_notification_show(title, subtitle, body, icon);
}

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
            prv_handle_pp(msg, dlen);
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
