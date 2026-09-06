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

"""Pytest fixtures for Tube DFS SAVE integration tests.

These tests verify that BASIC programs can be LOADed and SAVEd correctly
over DFS both with and without the Tube (65C02 second processor) active.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from oaknut.dfs import DFS, ACORN_DFS_40T_SINGLE_SIDED

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.screen import screen_contains, dump_screen

from adfs_test_support.basictool import build_basictool, tokenise
from adfs_test_support.disc_builder import build_test_disc

# Shared constants and the run_until_or_timeout helper live in
# tube_save_helpers.py so the test module can import them from a uniquely named
# module rather than from this conftest (a bare `from conftest import ...` is
# fragile when several conftests are collected together).
from tube_save_helpers import SSD_SIZE, run_until_or_timeout

# DFS ROM for the Acorn 1770 disc controller.
DFS_1770_ROM_FILENAME = "acorn-dfs_2_26.rom"

PROGRAMS_DIRPATH = Path(__file__).parent.parent / "programs"


# ---- Session-scope fixtures ----

@pytest.fixture(scope="session")
def basictool_filepath(tmp_path_factory):
    """Clone and build basictool, return the executable path."""
    build_dirpath = tmp_path_factory.mktemp("basictool")
    return build_basictool(build_dirpath)


@pytest.fixture(scope="session")
def test_program_bytes(basictool_filepath) -> bytes:
    """The tokenised BASIC test program."""
    source = (PROGRAMS_DIRPATH / "test_save.bas").read_text()
    return tokenise(source, basictool_filepath)


@pytest.fixture(scope="session")
def dfs_rom_filepath(beebium_roms_dirpath: Path) -> Path:
    """Path to the 1770 DFS ROM."""
    path = beebium_roms_dirpath / DFS_1770_ROM_FILENAME
    if not path.exists():
        pytest.skip(f"1770 DFS ROM not found: {path}")
    return path


# ---- Function-scope fixtures ----

@pytest.fixture
def source_ssd_filepath(tmp_path: Path, test_program_bytes: bytes) -> Path:
    """SSD containing the test program, written to a temp file."""
    ssd_bytes = build_test_disc("TEST", test_program_bytes)
    filepath = tmp_path / "source.ssd"
    filepath.write_bytes(ssd_bytes)
    return filepath


@pytest.fixture
def blank_ssd_filepath(tmp_path: Path) -> Path:
    """Blank formatted DFS SSD, written to a temp file."""
    buffer = bytearray(SSD_SIZE)
    DFS.from_buffer(memoryview(buffer), ACORN_DFS_40T_SINGLE_SIDED)
    filepath = tmp_path / "output.ssd"
    filepath.write_bytes(buffer)
    return filepath


# ---- Emulator fixtures ----

@pytest.fixture
def bbc_no_tube(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
    dfs_rom_filepath: Path,
    source_ssd_filepath: Path,
    blank_ssd_filepath: Path,
):
    """Model B with 1770 DFS, no Tube, two floppy drives."""
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=beebium_server_filepath,
            extra_args=[
                "--fdc", "acorn-1770",
                "--sideways", f"14:rom:{dfs_rom_filepath}",
                "--floppy", f"0:{source_ssd_filepath}",
                "--floppy", f"1:{blank_ssd_filepath}",
            ],
            startup_timeout=15.0,
        ) as bbc:
            bbc.debugger.stop()
            ok = bbc.run_until_or_timeout(
                lambda: screen_contains(bbc, ">"),
                emulated_seconds=10.0,
            )
            assert ok, f"Boot failed (no Tube):\n{dump_screen(bbc)}"
            yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))


@pytest.fixture
def bbc_with_tube(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
    dfs_rom_filepath: Path,
    source_ssd_filepath: Path,
    blank_ssd_filepath: Path,
):
    """Model B with 1770 DFS, Tube 65C02-3MHz, two floppy drives."""
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=beebium_server_filepath,
            extra_args=[
                "--tube-65c02",
                "--fdc", "acorn-1770",
                "--sideways", f"14:rom:{dfs_rom_filepath}",
                "--floppy", f"0:{source_ssd_filepath}",
                "--floppy", f"1:{blank_ssd_filepath}",
            ],
            startup_timeout=20.0,
        ) as bbc:
            ok = run_until_or_timeout(
                bbc,
                lambda: screen_contains(bbc, "Acorn TUBE"),
                emulated_seconds=30.0,
            )
            if not ok:
                print(f"Tube banner not seen:\n{dump_screen(bbc)}")
                pytest.fail("Tube banner not visible after boot")
            with bbc.debugger.running():
                yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))
