/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

PBL_DEVICE_STATE_DEFINE(bus);
PBL_DEVICE_STATE_DEFINE(intc);
PBL_DEVICE_STATE_DEFINE(rail);
static const struct pbl_i2c_bus bus = {.dev = PBL_DEVICE_INIT(bus, "i2c", nullptr, nullptr)};
static const struct pbl_intc intc = {.dev = PBL_DEVICE_INIT(intc, "intc", nullptr, nullptr)};
static const struct pbl_regulator rail = {.dev = PBL_DEVICE_INIT(rail, "rail", nullptr, nullptr)};

PBL_GT911_DEFINE(test_panel, &bus, 0x5d, PBL_IRQ_SPEC(&intc, 10, 3), PBL_GPIO(nullptr, 10, 0),
                 PBL_GPIO(nullptr, 4, PBL_GPIO_ACTIVE_LOW), &rail, 480, 800);
