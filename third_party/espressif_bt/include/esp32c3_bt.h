/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

//! The ESP32-C3 Bluetooth LE controller, from Espressif's library, and its VHCI transport.

#pragma once

#include <stdbool.h>
#include <stdint.h>

/** @brief What the controller calls, in its task. */
struct esp32c3_bt_vhci_callbacks {
  /** The controller can take another packet. */
  void (*send_available)(void);
  /** A whole H4 packet from the controller, its type byte first; return 0. */
  int (*recv)(uint8_t *data, uint16_t len);
};

/**
 * @brief Bring the controller up, guarded: power, clocks, controller, PHY and RF.
 *
 * Arms the RTC watchdog first and marks the attempt in a retained register: a hang, or no
 * esp32c3_bt_confirm() in time, resets the chip, and the next boot refuses to try again until
 * esp32c3_bt_clear(). Does nothing if the controller runs already.
 *
 * @return 0, -EPERM after a failed attempt, or the error of the step that failed.
 */
int esp32c3_bt_start(void);

/** @brief Stop the watchdog esp32c3_bt_start() armed: the console still works. */
void esp32c3_bt_confirm(void);

/** @brief Forget a failed attempt, and the request to use the controller at boot. */
void esp32c3_bt_clear(void);

/** @brief Ask for the controller to be used, from the next boot on. */
void esp32c3_bt_arm(void);

/** @brief Whether the controller was asked for at boot, and the last attempt did not fail. */
bool esp32c3_bt_armed(void);

/** @brief Whether a bring-up failed or hung, which keeps the controller off until cleared. */
bool esp32c3_bt_failed(void);

// The unguarded steps of esp32c3_bt_start(), derived from ESP-IDF's
int esp32c3_bt_controller_init(void);
int esp32c3_bt_controller_enable(void);
bool esp32c3_bt_controller_enabled(void);
const char *esp32c3_bt_controller_version(void);
/** @brief The source and the call count of the controller's i-th interrupt, while there is one. */
/** @brief The longest ISR and ISR-to-thread wake-up since the last call, in microseconds. */
void esp32c3_bt_timing_stats(uint32_t *isr_max_us, uint32_t *wake_max_us);
bool esp32c3_bt_intr_stats(uint32_t i, uint32_t *source, uint32_t *count);

/** @brief Whether the controller takes a packet now. */
bool esp32c3_bt_vhci_send_available(void);
/** @brief Send an H4 packet, its type byte first, to the controller. */
void esp32c3_bt_vhci_send(uint8_t *data, uint16_t len);
/** @brief Set the callbacks, once the controller is enabled. */
int esp32c3_bt_vhci_register(const struct esp32c3_bt_vhci_callbacks *cb);
