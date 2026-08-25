/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "pbl/util/attributes.h"
#include "system/reboot_reason.h"

NORETURN system_hard_reset(void);
NORETURN reset_due_to_software_failure(void);
