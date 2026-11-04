/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <pbl/logging/logging.h>
#include <pbl/util/math.h>

#include <applib/ui/vibes.h>
#ifdef CONFIG_SHELL
#include <pbl/shell/shell.h>
#endif

#include <host/ble_gap.h>
#include <host/ble_hs.h>
#include <host/ble_hs_adv.h>

#ifdef CONFIG_SHELL
static int prv_cmd_host_reset(const struct pbl_shell *sh, size_t argc, char **argv) {
  ble_hs_sched_reset(BLE_HS_EAPP);
  return 0;
}

PBL_SHELL_SUBCMD_ADD(sub_bt, host_reset, nullptr, "Reset the host stack", prv_cmd_host_reset, 0, 0);
#endif

// Index 01 ring: service UUID 607B5C9B-3700-4E94-F44A-2DF900BCB0C3, manufacturer
// data = company ID (2) + fingerprint (4) + collection count (1) + flags (1).
static const ble_uuid128_t s_index01_uuid = BLE_UUID128_INIT(
    0xc3, 0xb0, 0xbc, 0x00, 0xf9, 0x2d, 0x4a, 0xf4, 0x94, 0x4e, 0x00, 0x37, 0x9b, 0x5c, 0x7b, 0x60);
#define INDEX01_FLAG_RECORDING 0x20

#define INDEX01_MAX_RINGS 4

// Scan tuning: duty cycle = window / interval.
#define INDEX01_SCAN_ITVL_MS     480
#define INDEX01_SCAN_WIN_MS      60
#define INDEX01_SCAN_FILTER_DUPS 0
#define INDEX01_SCAN_PASSIVE     1

typedef struct {
  ble_addr_t addr;
  char name[24];
  uint32_t fingerprint;
  bool recording;
} Index01Ring;

// ponytail: small table keyed by address; a ring that rotates its random
// address shows up as a new entry.
static Index01Ring s_rings[INDEX01_MAX_RINGS];
static int s_num_rings;
static int s_next_slot;

static Index01Ring *prv_find_ring(const ble_addr_t *addr) {
  for (int i = 0; i < s_num_rings; i++) {
    if (ble_addr_cmp(&s_rings[i].addr, addr) == 0) {
      return &s_rings[i];
    }
  }
  return nullptr;
}

static bool prv_has_index01_uuid(const struct ble_hs_adv_fields *f) {
  for (int i = 0; i < f->num_uuids128; i++) {
    if (ble_uuid_cmp(&f->uuids128[i].u, &s_index01_uuid.u) == 0) {
      return true;
    }
  }
  return false;
}

static void prv_vibe(bool on) {
  vibes_cancel();
  if (on) {
    // A single step is capped at 10 s, which doubles as the auto-stop.
    static const uint32_t s_on_ms[] = {10000};
    vibes_enqueue_custom_pattern((VibePattern){.durations = s_on_ms, .num_segments = 1});
  }
}

static bool prv_any_recording(void) {
  for (int i = 0; i < s_num_rings; i++) {
    if (s_rings[i].recording) {
      return true;
    }
  }
  return false;
}

static bool s_scan_wanted;

static int prv_index_scan_event(struct ble_gap_event *event, void *arg);

static int prv_start_scan(void) {
  uint8_t own_addr_type;
  int rc = ble_hs_id_infer_auto(0, &own_addr_type);
  if (rc != 0) {
    return rc;
  }

  const struct ble_gap_disc_params params = {
    .itvl = BLE_GAP_SCAN_ITVL_MS(INDEX01_SCAN_ITVL_MS),
    .window = BLE_GAP_SCAN_WIN_MS(INDEX01_SCAN_WIN_MS),
    .filter_duplicates = INDEX01_SCAN_FILTER_DUPS,
    .passive = INDEX01_SCAN_PASSIVE,
  };
  return ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &params, prv_index_scan_event, nullptr);
}

static int prv_index_scan_event(struct ble_gap_event *event, void *arg) {
  if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
    // The host ends discovery when it preempts GAP, e.g. to update the
    // resolving list after bonding; resume it.
    int rc = s_scan_wanted ? prv_start_scan() : 0;
    PBL_LOG_INFO("Index 01 scan ended reason=%d, restart rc=%d", event->disc_complete.reason, rc);
    return 0;
  }
  if (event->type != BLE_GAP_EVENT_DISC) {
    return 0;
  }

  struct ble_hs_adv_fields f;
  if (ble_hs_adv_parse_fields(&f, event->disc.data, event->disc.length_data) != 0) {
    return 0;
  }

  const ble_addr_t *addr = &event->disc.addr;
  Index01Ring *ring = prv_find_ring(addr);

  // UUID, name and manufacturer data may arrive in separate ADV / SCAN_RSP packets.
  if (ring == nullptr) {
    if (!prv_has_index01_uuid(&f)) {
      return 0;
    }
    // Rings rotate their random address; once full, reuse the oldest slot.
    if (s_num_rings < INDEX01_MAX_RINGS) {
      s_num_rings++;
    }
    ring = &s_rings[s_next_slot];
    s_next_slot = (s_next_slot + 1) % INDEX01_MAX_RINGS;
    *ring = (Index01Ring){.addr = *addr};
    char addr_str[18];
    snprintf(addr_str, sizeof(addr_str), "%02x:%02x:%02x:%02x:%02x:%02x", addr->val[5],
             addr->val[4], addr->val[3], addr->val[2], addr->val[1], addr->val[0]);
    PBL_LOG_INFO("Index 01 #%d found addr=%s type=%u rssi=%d", (int)(ring - s_rings), addr_str,
                 addr->type, event->disc.rssi);
  }

  if (f.name != NULL && ring->name[0] == '\0') {
    size_t len = MIN(f.name_len, sizeof(ring->name) - 1);
    memcpy(ring->name, f.name, len);
    ring->name[len] = '\0';
    PBL_LOG_INFO("Index 01 #%d name='%s'", (int)(ring - s_rings), ring->name);
  }

  if (f.mfg_data == NULL || f.mfg_data_len < 8) {
    return 0;
  }

  uint32_t fingerprint = f.mfg_data[2] | (f.mfg_data[3] << 8) | (f.mfg_data[4] << 16) |
                         ((uint32_t)f.mfg_data[5] << 24);
  if (fingerprint != ring->fingerprint) {
    ring->fingerprint = fingerprint;
    PBL_LOG_INFO("Index 01 #%d company=0x%04x fingerprint=0x%08" PRIx32, (int)(ring - s_rings),
                 f.mfg_data[0] | (f.mfg_data[1] << 8), fingerprint);
  }

  bool recording = (f.mfg_data[7] & INDEX01_FLAG_RECORDING) != 0;
  if (recording != ring->recording) {
    // One motor for all rings: run it while any of them is held.
    bool was_held = prv_any_recording();
    ring->recording = recording;
    if (prv_any_recording() != was_held) {
      prv_vibe(!was_held);
    }
    PBL_LOG_INFO("Index 01 #%d '%s' button %s rssi=%d flags=0x%02x", (int)(ring - s_rings),
                 ring->name, recording ? "DOWN" : "UP", event->disc.rssi, f.mfg_data[7]);
  }
  return 0;
}

// ponytail: always-on proof of concept so release builds (no console) scan.
void pbl_bt_index01_scan_start(void) {
  s_scan_wanted = true;
  int rc = prv_start_scan();
  PBL_LOG_INFO("Index 01 scan started rc=%d", rc);
}

#ifdef CONFIG_SHELL
static int prv_cmd_index_scan(const struct pbl_shell *sh, size_t argc, char **argv) {
  int rc;

  if (strcmp(argv[1], "off") == 0) {
    s_scan_wanted = false;
    rc = ble_gap_disc_cancel();
    prv_vibe(false);
    // Discovery is cancelled, so the scan callback no longer touches the table.
    s_num_rings = 0;
    s_next_slot = 0;
    pbl_shell_print(sh, "scan stopped rc=%d", rc);
    return 0;
  }
  if (strcmp(argv[1], "on") != 0) {
    pbl_shell_error(sh, "invalid mode '%s'", argv[1]);
    return -EINVAL;
  }

  s_scan_wanted = true;
  rc = prv_start_scan();
  pbl_shell_print(sh, "scan started rc=%d", rc);
  return rc == 0 ? 0 : -EIO;
}

PBL_SHELL_SUBCMD_ADD(sub_bt, index_scan, nullptr, "Scan for Index 01 button presses <on|off>",
                     prv_cmd_index_scan, 2, 0);

#endif
