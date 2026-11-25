# SPDX-FileCopyrightText: 2026 Core Devices LLC
# SPDX-License-Identifier: Apache-2.0

from harness.helpers.ui import Button


def test_version(prompt, build):
    response = "\n".join(prompt("version"))
    assert "Running FW:" in response
    assert "tag:v" in response


def test_logs_stream(dut):
    from libpebble2.protocol.blobdb import BlobDatabaseID
    from libpebble2.services.blobdb import BlobDBClient, SyncWrapper

    # Clearing a blob DB is logged, and changes nothing on screen.
    since = dut.logs.mark()
    SyncWrapper(BlobDBClient(dut.protocol).clear, BlobDatabaseID.Notification).wait()
    dut.wait_for_log(r"Flushing BlobDB", timeout=30, since=since)


def test_reset(dut, ui):
    ui.press(Button.SELECT)
    ui.wait_idle()
    ui.press(Button.SELECT)
    ui.wait_idle()
    assert ui.top_window() == "Settings"
    dut.reset()
    ui.wait_idle(timeout=30)
    assert "Settings" not in ui.window_stack()


def test_xtensa_resource_mpu_layout(build):
    """The flash window can include two banks outside the executable firmware partition."""
    import struct

    import pytest
    from elftools.elf.elffile import ELFFile
    from pbl.flash_layout import regions

    if not build.config.get("CONFIG_CPU_XTENSA"):
        pytest.skip("Xtensa flash window")

    with open(build.elf, "rb") as stream:
        elf = ELFFile(stream)
        tables = elf.get_section_by_name(".symtab").get_symbol_by_name("s_kernel_regions")
        assert tables, "missing static MPU region descriptors"
        table = tables[0]
        section = elf.get_section(table["st_shndx"])
        offset = table["st_value"] - section["sh_addr"]
        # The first struct pbl_mpu_region describes firmware: name pointer, base, size, flags.
        _, base, size, _ = struct.unpack_from("<4I", section.data(), offset)

    cfg = build.config
    assert base == cfg["CONFIG_FLASH_BASE"] + cfg["CONFIG_FLASH_OFFSET"]
    assert size == cfg["CONFIG_FW_FLASH_SIZE"]
    partitions = regions(build)
    for bank in ("SYSTEM_RESOURCES_BANK_0", "SYSTEM_RESOURCES_BANK_1"):
        start, length = partitions[bank]
        assert base + size <= cfg["CONFIG_FLASH_BASE"] + start
        if cfg.get("CONFIG_MMAP_RESOURCES"):
            assert start + length <= cfg["CONFIG_FLASH_SIZE"]
