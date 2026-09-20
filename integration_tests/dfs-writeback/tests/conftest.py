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

"""Pytest fixtures for DFS disc write-back integration tests.

These tests reproduce a class of bug where data written to an Acorn DFS .ssd
disc image is not flushed all the way through to the host filesystem once drive
activity has ended. They use oaknut-dfs to read the host .ssd file as an
independent, out-of-process check on what Beebium has actually persisted.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

import pytest

from oaknut.dfs import DFS, ACORN_DFS_40T_SINGLE_SIDED

from beebium.client import Beebium
from beebium.client.screen import screen_contains, dump_screen


# DFS ROM goes in slot 11 (IC52) on the B+, matching the ADFS suite's layout.
DFS_SLOT = 11

# 40-track single-sided: 40 * 10 * 256 = 102400 bytes.
SSD_SIZE = 102400


# ---- Session-scope fixtures: ROMs and server ----

@pytest.fixture(scope="session")
def roms_dirpath():
    """Path to the ROM directory."""
    env = os.environ.get("BEEBIUM_ROM_DIR")
    if env:
        p = Path(env)
        if p.is_dir():
            return p
        raise FileNotFoundError(f"BEEBIUM_ROM_DIR={env} is not a directory")

    repo_root = Path(__file__).parent.parent.parent.parent
    candidate = repo_root / "roms"
    if candidate.is_dir():
        return candidate
    raise FileNotFoundError(
        "ROM directory not found. Set BEEBIUM_ROM_DIR or place ROMs in roms/"
    )


@pytest.fixture(scope="session")
def mos_filepath(roms_dirpath):
    """Path to the Model B+ MOS 2.0 ROM."""
    p = roms_dirpath / "acorn-mos_2_0.rom"
    if not p.exists():
        pytest.skip(f"MOS 2.0 ROM not found: {p}")
    return p


@pytest.fixture(scope="session")
def basic_filepath(roms_dirpath):
    """Path to the BBC BASIC 2 ROM."""
    p = roms_dirpath / "bbc-basic_2.rom"
    if not p.exists():
        pytest.skip(f"BASIC 2 ROM not found: {p}")
    return p


@pytest.fixture(scope="session")
def dfs_rom_filepath(roms_dirpath):
    """Path to the DFS 2.26 ROM (the correct DFS for the WD1770)."""
    p = roms_dirpath / "acorn-dfs_2_26.rom"
    if not p.exists():
        pytest.skip(f"DFS 2.26 ROM not found: {p}")
    return p


@pytest.fixture(scope="session")
def bplus_server_filepath():
    """Path to the beebium-model-b-plus server executable."""
    env = os.environ.get("BEEBIUM_SERVER_BPLUS")
    if env:
        p = Path(env)
        if p.exists():
            return p
        raise FileNotFoundError(f"BEEBIUM_SERVER_BPLUS={env} not found")

    env = os.environ.get("BEEBIUM_SERVER")
    if env:
        p = Path(env.replace("beebium-model-b", "beebium-model-b-plus"))
        if p.exists():
            return p

    repo_root = Path(__file__).parent.parent.parent.parent
    exe_suffix = ".exe" if sys.platform == "win32" else ""
    candidates = [
        repo_root / "build" / "src" / "server" / f"beebium-model-b-plus{exe_suffix}",
        repo_root / "cmake-build-debug" / "src" / "server" / f"beebium-model-b-plus{exe_suffix}",
    ]
    for c in candidates:
        if c.exists():
            return c

    pytest.skip(
        "beebium-model-b-plus not found. Set BEEBIUM_SERVER_BPLUS or build the server."
    )


# ---- Function-scope fixtures: blank disc and booted machine ----

@pytest.fixture
def blank_ssd_filepath(tmp_path):
    """Create a fresh, blank, writable 40-track DFS SSD for this test.

    The image is formatted with an empty DFS catalogue by oaknut-dfs so that
    DFS recognises it immediately, with no files present.
    """
    ssd_filepath = tmp_path / "writeback.ssd"
    buffer = bytearray(SSD_SIZE)
    # from_buffer on a zeroed buffer writes a valid empty DFS catalogue.
    DFS.from_buffer(memoryview(buffer), ACORN_DFS_40T_SINGLE_SIDED)
    ssd_filepath.write_bytes(bytes(buffer))
    # Ensure the file is writable so DFS does not treat the disc as
    # write-protected.
    ssd_filepath.chmod(0o644)
    return ssd_filepath


@pytest.fixture
def bbc_dfs(bplus_server_filepath, mos_filepath, basic_filepath,
            dfs_rom_filepath, blank_ssd_filepath):
    """Launch Model B+ with DFS 2.26 and a blank writable SSD in drive 0."""
    extra_args = [
        "--sideways", f"slot={DFS_SLOT}:type=rom:image={dfs_rom_filepath}",
        "--floppy", f"0:{blank_ssd_filepath}",
    ]

    with Beebium.launch(
        mos_filepath=mos_filepath,
        basic_filepath=basic_filepath,
        server_filepath=bplus_server_filepath,
        extra_args=extra_args,
        startup_timeout=15.0,
    ) as bbc:
        # Remove the ~1.2s spin-up delay so the disc operations run quickly
        # in emulated time.
        bbc.disc.set_spin_up_delay(False)
        bbc.debugger.stop()
        ok = bbc.run_until_or_timeout(
            lambda: screen_contains(bbc, ">"),
            emulated_seconds=10.0,
        )
        assert ok, f"Boot to BASIC prompt failed:\n{dump_screen(bbc)}"
        yield bbc
