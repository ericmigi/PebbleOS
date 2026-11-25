/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

/**
 * @defgroup soc_esp32c3 ESP32-C3
 * @ingroup soc
 * @brief Espressif ESP32-C3 SoC interfaces.
 * @{
 */

/** @brief Idle states, see pbl_idle_state_block(). */
enum soc_esp32c3_idle_state {
  /** Wait for an interrupt. */
  SOC_ESP32C3_IDLE_WFI,
  /** Light sleep, clocks of the digital domain stopped. */
  SOC_ESP32C3_IDLE_LIGHT_SLEEP,
};

/** @} */
