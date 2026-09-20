# Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
#
# This file is part of Beebium.
#
# Beebium is free software: you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version. Beebium is distributed in the hope that it will
# be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# You should have received a copy of the GNU General Public License along with Beebium.
# If not, see <https://www.gnu.org/licenses/>.

"""DFS disc loading tests.

Tests that Beebium can correctly load and auto-boot programs from SSD images,
including truncated images produced by BeebAsm.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.screen import screen_contains, dump_screen, read_mode7_screen


ASM_DIRPATH = Path(__file__).parent.parent / "asm"


@pytest.mark.slow
@pytest.mark.timeout(60)
def test_boot_from_full_size_ssd(
    server_filepath, mos_filepath, basic_filepath, dfs_filepath,
):
    """Auto-boot a program from a full-size (102400 byte) SSD created by oaknut-dfs."""
    ssd_filepath = ASM_DIRPATH / "test_hello_proper.ssd"
    if not ssd_filepath.exists():
        pytest.skip(f"Test SSD not found: {ssd_filepath}")

    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=server_filepath,
            extra_args=[
                "--fdc", "acorn-1770",
                "--sideways", f"slot=11:type=rom:image={dfs_filepath}",
                "--floppy", f"0:{ssd_filepath}",
                "--auto-boot",
            ],
            startup_timeout=15.0,
        ) as bbc:
            bbc.debugger.stop()
            ok = bbc.run_until_or_timeout(
                lambda: screen_contains(bbc, "HELLO DONE"),
                emulated_seconds=15.0,
            )

            screen = dump_screen(bbc)
            print(f"Screen:\n{screen}")
            assert ok, f"Program did not produce expected output:\n{screen}"

    except ServerNotFoundError as e:
        pytest.skip(str(e))


@pytest.mark.slow
@pytest.mark.timeout(60)
def test_boot_from_truncated_beebasm_ssd(
    server_filepath, mos_filepath, basic_filepath, dfs_filepath,
):
    """Auto-boot a program from a truncated SSD produced by BeebAsm.

    BeebAsm creates minimal disc images containing only the sectors needed
    for the catalogue and file data. Beebium's SsdFormatHandler claims to
    support these by zero-filling missing sectors.

    Uses only DFS + floppy (no ADFS, no SCSI) to isolate the disc loading.
    """
    ssd_filepath = ASM_DIRPATH / "test_hello.ssd"
    if not ssd_filepath.exists():
        pytest.skip(f"Test SSD not found: {ssd_filepath}")

    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=server_filepath,
            extra_args=[
                "--fdc", "acorn-1770",
                "--sideways", f"slot=11:type=rom:image={dfs_filepath}",
                "--floppy", f"0:{ssd_filepath}",
                "--auto-boot",
            ],
            startup_timeout=15.0,
        ) as bbc:
            bbc.debugger.stop()
            ok = bbc.run_until_or_timeout(
                lambda: screen_contains(bbc, "HELLO DONE"),
                emulated_seconds=15.0,
            )

            screen = dump_screen(bbc)
            print(f"Screen:\n{screen}")
            if not ok:
                rows = read_mode7_screen(bbc)
                screen_text = "\n".join(rows)
                if "Disc fault" in screen_text:
                    pytest.fail(
                        f"DFS reported disc fault loading truncated SSD.\n{screen}"
                    )
                pytest.fail(f"Program did not complete:\n{screen}")

    except ServerNotFoundError as e:
        pytest.skip(str(e))


@pytest.mark.slow
@pytest.mark.timeout(60)
def test_boot_from_truncated_ssd_with_adfs_rom_no_scsi(
    server_filepath, mos_filepath, basic_filepath,
    dfs_filepath, adfs_filepath,
):
    """Auto-boot truncated SSD with ADFS ROM present but no SCSI disc."""
    ssd_filepath = ASM_DIRPATH / "test_hello.ssd"
    if not ssd_filepath.exists():
        pytest.skip(f"Test SSD not found: {ssd_filepath}")

    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=server_filepath,
            extra_args=[
                "--fdc", "acorn-1770",
                "--sideways", f"slot=10:type=rom:image={adfs_filepath}",
                "--sideways", f"slot=11:type=rom:image={dfs_filepath}",
                "--floppy", f"0:{ssd_filepath}",
                "--auto-boot",
            ],
            startup_timeout=15.0,
        ) as bbc:
            bbc.debugger.stop()
            ok = bbc.run_until_or_timeout(
                lambda: screen_contains(bbc, "HELLO DONE"),
                emulated_seconds=15.0,
            )

            screen = dump_screen(bbc)
            print(f"Screen:\n{screen}")
            if not ok:
                rows = read_mode7_screen(bbc)
                screen_text = "\n".join(rows)
                if "Disc fault" in screen_text:
                    pytest.fail(
                        f"DFS disc fault with ADFS ROM but no SCSI.\n{screen}"
                    )
                pytest.fail(f"Program did not complete:\n{screen}")

    except ServerNotFoundError as e:
        pytest.skip(str(e))


@pytest.mark.slow
@pytest.mark.timeout(60)
def test_boot_from_truncated_ssd_with_adfs_and_scsi(
    server_filepath, mos_filepath, basic_filepath,
    dfs_filepath, adfs_filepath, scsi_hdd_filepath,
):
    """Auto-boot truncated SSD with ADFS ROM AND SCSI disc present."""
    ssd_filepath = ASM_DIRPATH / "test_hello.ssd"
    if not ssd_filepath.exists():
        pytest.skip(f"Test SSD not found: {ssd_filepath}")

    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=server_filepath,
            extra_args=[
                "--fdc", "acorn-1770",
                "--sideways", f"slot=10:type=rom:image={adfs_filepath}",
                "--sideways", f"slot=11:type=rom:image={dfs_filepath}",
                "--acorn-scsi",
                "--scsi-hdd", f"0:{scsi_hdd_filepath}",
                "--floppy", f"0:{ssd_filepath}",
                "--auto-boot",
            ],
            startup_timeout=15.0,
        ) as bbc:
            bbc.debugger.stop()
            ok = bbc.run_until_or_timeout(
                lambda: screen_contains(bbc, "HELLO DONE"),
                emulated_seconds=15.0,
            )

            screen = dump_screen(bbc)
            print(f"Screen:\n{screen}")
            if not ok:
                rows = read_mode7_screen(bbc)
                screen_text = "\n".join(rows)
                if "Disc fault" in screen_text:
                    pytest.fail(
                        f"DFS disc fault with ADFS ROM + SCSI.\n{screen}"
                    )
                pytest.fail(f"Program did not complete:\n{screen}")

    except ServerNotFoundError as e:
        pytest.skip(str(e))
