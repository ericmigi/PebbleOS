/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "process_management/app_install_types.h"

//! Install the port's bundled test PBW as a normal PFS/AppDB application.
//! Returns INSTALL_ID_INVALID on failure.
AppInstallId fw_appdb_install_test_app(void);
