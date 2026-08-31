/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "process_management/pebble_process_md.h"

//! Launch a PRIVILEGED, built-in system app (a watchface, settings, music, ...)
//! from its real PebbleOS PebbleProcessMd. This is the port's analog of the
//! shipping app_manager/process_manager launch path (see
//! zephyr-port-notes/SYSTEM-APPS-BUILDOUT.md): it hands the shared window stack
//! + KernelMain UI event loop to md->main_func(), renders on the panel, and
//! returns here when the app exits (BACK past its root window). Runs the app on
//! the KernelMain task privileged (no MPU sandbox), unlike fw_sandbox_launch().
void fw_system_app_launch(const PebbleProcessMd *md);

//! Pop the running app's windows down to its launch base so its app_event_loop
//! returns (used when another app is launched out of the current one).
void fw_system_app_request_exit(void);
