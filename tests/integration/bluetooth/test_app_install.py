# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

"""A third-party app installed from a .pbw over Bluetooth, as the phone app
installs it: the app DB record, the binary and resources over PutBytes,
then a launch. The .pbw is given with PBL_ITEST_PBW; there is none in the
tree."""

import logging
import os
import re
import time
import uuid

import pytest
from harness.helpers.blobdb import BlobDB, Database, Status

APP_RUN_STATE_ENDPOINT = 0x0034
RUN_STATE_START = 1
APP_START_S = 30

logger = logging.getLogger(__name__)


@pytest.fixture
def pbw():
    path = os.getenv("PBL_ITEST_PBW")
    if not path:
        pytest.skip("no PBL_ITEST_PBW")
    return path


def _active_app(prompt):
    for line in prompt("app active"):
        if m := re.match(r"app name: (.*)", line):
            return m.group(1)
    return None


def test_install_and_launch(dut, prompt, phone, pbw):
    from libpebble2.services.install import AppInstaller

    installer = AppInstaller(phone.pebble, pbw)
    metadata = installer._bundle.get_app_metadata()
    app_uuid = uuid.UUID(str(metadata["uuid"]))
    name = metadata["app_name"]
    start = time.monotonic()
    installer.install()
    logger.info("installed %s in %.1f s over Bluetooth", name, time.monotonic() - start)
    try:
        assert any(name in line for line in prompt("app list"))
        phone.send(APP_RUN_STATE_ENDPOINT, bytes([RUN_STATE_START]) + app_uuid.bytes)
        deadline = time.monotonic() + APP_START_S
        while _active_app(prompt) != name:
            assert time.monotonic() < deadline, f"running {_active_app(prompt)!r}"
            time.sleep(0.5)
        dut.screenshot().save(os.path.join(dut.config.results_dir, "app_install.png"))
    finally:
        assert BlobDB(phone).delete(Database.APPS, app_uuid) == Status.SUCCESS
