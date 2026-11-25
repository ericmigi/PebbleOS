# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

from harness.device.hardware_adapter import HardwareAdapter
from harness.device.native_adapter import NativeAdapter
from harness.device.qemu_adapter import QemuAdapter
from harness.device.qemu_esp32_adapter import QemuEsp32Adapter

ADAPTERS = {
    adapter.type: adapter for adapter in (QemuAdapter, NativeAdapter, HardwareAdapter)
}


def get_device(device_type, build=None):
    if device_type == "qemu" and build is not None and build.qemu_esp32:
        return QemuEsp32Adapter
    return ADAPTERS[device_type]
