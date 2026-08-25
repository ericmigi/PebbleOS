/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>

#include "process_management/app_install_types.h"

bool fw_sandbox_launch(AppInstallId install_id);

// Initialise the panel + applib graphics shell (framebuffer, GContext, fonts,
// app heap) and turn the display on. Safe to call more than once.
void fw_sandbox_display_init(void);
