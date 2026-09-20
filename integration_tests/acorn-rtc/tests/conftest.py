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

"""Pytest fixtures for User Port RTC integration tests.

Launches a Model B with DFS, the RTC extension, and a test BASIC program
on floppy. The RTC is initialised to a known time for deterministic testing.
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

from rtc_test_support import PROGRAMS_DIRPATH


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
    """Path to the Model B MOS 1.20 ROM."""
    p = roms_dirpath / "acorn-mos_1_20.rom"
    if not p.exists():
        pytest.skip(f"MOS 1.20 ROM not found: {p}")
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
    """Path to the DFS 2.26 ROM."""
    p = roms_dirpath / "acorn-dfs_2_26.rom"
    if not p.exists():
        pytest.skip(f"DFS 2.26 ROM not found: {p}")
    return p


@pytest.fixture(scope="session")
def model_b_server_filepath():
    """Path to the beebium-model-b server executable."""
    env = os.environ.get("BEEBIUM_SERVER")
    if env:
        p = Path(env)
        if p.exists():
            return p
        raise FileNotFoundError(f"BEEBIUM_SERVER={env} not found")
    repo_root = Path(__file__).parent.parent.parent.parent
    exe_suffix = ".exe" if sys.platform == "win32" else ""
    candidates = [
        repo_root / "build" / "src" / "server" / f"beebium-model-b{exe_suffix}",
        repo_root / "cmake-build-debug" / "src" / "server" / f"beebium-model-b{exe_suffix}",
    ]
    for c in candidates:
        if c.exists():
            return c
    pytest.skip(
        "beebium-model-b not found. Set BEEBIUM_SERVER or build the server."
    )


@pytest.fixture(scope="session")
def extension_dirpath():
    """Path to the directory containing extension plugin shared libraries."""
    env = os.environ.get("BEEBIUM_EXTENSION_DIR")
    if env:
        p = Path(env)
        if p.is_dir():
            return p
        raise FileNotFoundError(f"BEEBIUM_EXTENSION_DIR={env} is not a directory")
    repo_root = Path(__file__).parent.parent.parent.parent
    candidates = [
        repo_root / "build" / "src" / "extensions" / "acorn-rtc",
    ]
    for c in candidates:
        if c.is_dir():
            return c
    pytest.skip(
        "Extension directory not found. Set BEEBIUM_EXTENSION_DIR or build."
    )


DFS_SLOT = 13  # IC88 on Model B


@pytest.fixture
def test_disc_ssd(basictool_filepath):
    """Tokenise test_rtc.bas and build a DFS SSD."""
    source = (PROGRAMS_DIRPATH / "test_rtc.bas").read_text()
    tokenised = tokenise(source, basictool_filepath)
    return build_test_disc("TEST", tokenised)


@pytest.fixture
def bbc_rtc(model_b_server_filepath, mos_filepath, basic_filepath,
            dfs_rom_filepath, extension_dirpath, test_disc_ssd, tmp_path):
    """Launch Model B with DFS + RTC extension + test program.

    The RTC is initialised to 1985-06-15T14:30 for deterministic testing.
    """
    ssd_filepath = tmp_path / "test.ssd"
    ssd_filepath.write_bytes(test_disc_ssd)

    extra_args = [
        "--sideways", f"slot={DFS_SLOT}:type=rom:image={dfs_rom_filepath}",
        "--fdc", "acorn-1770",
        "--floppy", f"0:{ssd_filepath}",
        "--extension-dir", str(extension_dirpath),
        "--acorn-rtc", "time=1985-06-15T1430",
    ]

    with Beebium.launch(
        mos_filepath=mos_filepath,
        basic_filepath=basic_filepath,
        server_filepath=model_b_server_filepath,
        extra_args=extra_args,
        startup_timeout=15.0,
    ) as bbc:
        bbc.debugger.stop()
        ok = bbc.run_until_or_timeout(
            lambda: screen_contains(bbc, ">"),
            emulated_seconds=10.0,
        )
        assert ok, f"Boot to BASIC prompt failed:\n{dump_screen(bbc)}"
        yield bbc
