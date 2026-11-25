/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

//! Complete an HCI command locally, as the fake controller does, and free it.
int hci_fake_cmd(void *buf);
