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

"""Pytest fixtures for ADFS integration tests.

These fixtures manage the full test lifecycle:
- Building basictool (session-scope, once per test run)
- Extracting blank ADFS hard disc images (per-test isolation)
- Building DFS floppy discs with tokenised BASIC test programs
- Launching the emulator with the correct ROM and extension configuration
- Waiting for boot and providing screen-reading helpers
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.screen import screen_contains, dump_screen

from adfs_test_support.basictool import build_basictool, tokenise
from adfs_test_support.disc_builder import build_test_disc
from adfs_disc_tools import extract_blank_adfs_image


# ---- Session-scope fixtures ----

@pytest.fixture(scope="session")
def basictool_filepath(tmp_path_factory):
    """Clone and build basictool, return the executable path."""
    build_dirpath = tmp_path_factory.mktemp("basictool")
    return build_basictool(build_dirpath)


@pytest.fixture(scope="session")
def roms_dirpath():
    """Path to the ROM directory."""
    env = os.environ.get("BEEBIUM_ROM_DIR")
    if env:
        p = Path(env)
        if p.is_dir():
            return p
        raise FileNotFoundError(f"BEEBIUM_ROM_DIR={env} is not a directory")

    # Try common locations relative to the repo root
    repo_root = Path(__file__).parent.parent.parent.parent
    candidates = [repo_root / "roms"]
    for c in candidates:
        if c.is_dir():
            return c
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
def adfs_rom_filepath(roms_dirpath):
    """Path to the ADFS 1.30 ROM."""
    p = roms_dirpath / "acorn-adfs_1_30.rom"
    if not p.exists():
        pytest.skip(f"ADFS 1.30 ROM not found: {p}")
    return p


@pytest.fixture(scope="session")
def dfs_rom_filepath(roms_dirpath):
    """Path to the DFS 2.26 ROM."""
    p = roms_dirpath / "acorn-dfs_2_26.rom"
    if not p.exists():
        pytest.skip(f"DFS 2.26 ROM not found: {p}")
    return p


@pytest.fixture(scope="session")
def bplus_server_filepath():
    """Path to the beebium-model-b-plus server executable."""
    # Try BEEBIUM_SERVER_BPLUS first
    env = os.environ.get("BEEBIUM_SERVER_BPLUS")
    if env:
        p = Path(env)
        if p.exists():
            return p
        raise FileNotFoundError(f"BEEBIUM_SERVER_BPLUS={env} not found")

    # Derive from BEEBIUM_SERVER by replacing model-b with model-b-plus
    env = os.environ.get("BEEBIUM_SERVER")
    if env:
        p = Path(env.replace("beebium-model-b", "beebium-model-b-plus"))
        if p.exists():
            return p

    # Try common build locations
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
        "beebium-model-b-plus not found. "
        "Set BEEBIUM_SERVER_BPLUS or build the server."
    )


# ---- Function-scope fixtures ----

@pytest.fixture
def blank_scsi_disc(tmp_path):
    """Extract a fresh blank 2 MB ADFS hard disc image for this test."""
    return extract_blank_adfs_image(tmp_path / "scsi0.dat", size_mb=2)


@pytest.fixture
def blank_scsi_disc_4mb(tmp_path):
    """Extract a fresh blank 4 MB ADFS hard disc image for this test."""
    return extract_blank_adfs_image(tmp_path / "scsi0.dat", size_mb=4)


@pytest.fixture
def blank_scsi_disc_8mb(tmp_path):
    """Extract a fresh blank 8 MB ADFS hard disc image for this test."""
    return extract_blank_adfs_image(tmp_path / "scsi0.dat", size_mb=8)


from adfs_test_support import PROGRAMS_DIRPATH  # noqa: F401 (re-exported for tests)

# ADFS ROM goes in slot 9 (IC62). Slots 14/15 are shared with BASIC on the B+.
ADFS_SLOT = 9
DFS_SLOT = 11


def _make_bbc_adfs(bplus_server_filepath, mos_filepath, basic_filepath,
                   adfs_rom_filepath, dfs_rom_filepath,
                   scsi_dat_filepath, test_disc_ssd, tmp_path):
    """Launch Model B+ with ADFS + SCSI + test program on floppy.

    Returns a context manager yielding the Beebium instance.
    """
    ssd_filepath = tmp_path / "test.ssd"
    ssd_filepath.write_bytes(test_disc_ssd)

    extra_args = [
        "--sideways", f"slot={ADFS_SLOT}:type=rom:image={adfs_rom_filepath}",
        "--sideways", f"slot={DFS_SLOT}:type=rom:image={dfs_rom_filepath}",
        "--floppy", f"0:{ssd_filepath}",
        "--acorn-scsi",
        "--scsi-hdd", f"0:{scsi_dat_filepath}",
    ]

    return Beebium.launch(
        mos_filepath=mos_filepath,
        basic_filepath=basic_filepath,
        server_filepath=bplus_server_filepath,
        extra_args=extra_args,
        startup_timeout=15.0,
    )


@pytest.fixture
def bbc_adfs(bplus_server_filepath, mos_filepath, basic_filepath,
             adfs_rom_filepath, dfs_rom_filepath,
             blank_scsi_disc, test_disc_ssd, tmp_path):
    """Launch Model B+ with ADFS on a blank 2 MB SCSI disc + test program."""
    with _make_bbc_adfs(bplus_server_filepath, mos_filepath, basic_filepath,
                        adfs_rom_filepath, dfs_rom_filepath,
                        blank_scsi_disc, test_disc_ssd, tmp_path) as bbc:
        bbc.debugger.stop()
        ok = bbc.run_until_or_timeout(
            lambda: screen_contains(bbc, ">"),
            emulated_seconds=10.0,
        )
        assert ok, f"Boot failed:\n{dump_screen(bbc)}"
        yield bbc


@pytest.fixture
def bbc_adfs_4mb(bplus_server_filepath, mos_filepath, basic_filepath,
                 adfs_rom_filepath, dfs_rom_filepath,
                 blank_scsi_disc_4mb, test_disc_ssd, tmp_path):
    """Launch Model B+ with ADFS on a blank 4 MB SCSI disc + test program."""
    with _make_bbc_adfs(bplus_server_filepath, mos_filepath, basic_filepath,
                        adfs_rom_filepath, dfs_rom_filepath,
                        blank_scsi_disc_4mb, test_disc_ssd, tmp_path) as bbc:
        bbc.debugger.stop()
        ok = bbc.run_until_or_timeout(
            lambda: screen_contains(bbc, ">"),
            emulated_seconds=10.0,
        )
        assert ok, f"Boot failed:\n{dump_screen(bbc)}"
        yield bbc


