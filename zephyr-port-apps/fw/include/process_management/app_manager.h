/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "kernel/events.h"

#include "applib/app_launch_button.h"
#include "applib/app_launch_reason.h"
#include "process_management/app_install_types.h"

// Only reached for App/Worker-targeted timers; the port drives KernelMain only.
bool process_manager_send_event_to_process(PebbleTask task, PebbleEvent *e);

// Minimal launch-config shape the ported system apps use (watchfaces picker).
// The shipping LaunchConfigCommon also carries compositor-transition + args
// pointers the port has no compositor for; those are omitted here.
// ponytail: no transition/args. Include the real launch_config.h once the
// compositor is ported.
typedef struct LaunchConfigCommon {
  AppLaunchReason reason;
  ButtonId button;
} LaunchConfigCommon;

typedef struct AppLaunchEventConfig {
  LaunchConfigCommon common;
  AppInstallId id;
} AppLaunchEventConfig;

//! Launch (or, in the port, select) an installed app by id. See
//! apps_port_glue.c: records the chosen watchface as the default face.
void app_manager_put_launch_app_event(const AppLaunchEventConfig *config);
