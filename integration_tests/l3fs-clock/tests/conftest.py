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

"""Pytest fixtures for L3 File Server clock update timing tests.

These tests are long-running (several minutes) and require external resources
(ROMs, disc images). They are disabled by default and must be run explicitly
with ``pytest -m slow``.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

import pytest


def pytest_collection_modifyitems(config, items):
    """Skip tests marked 'slow' unless explicitly requested."""
    if config.getoption("-m", default="") and "slow" in config.getoption("-m"):
        return
    skip_slow = pytest.mark.skip(reason="slow test -- run with pytest -m slow")
    for item in items:
        if "slow" in item.keywords:
            item.add_marker(skip_slow)


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
def server_filepath():
    """Path to the beebium-model-b-romram server executable."""
    env = os.environ.get("BEEBIUM_SERVER")
    if env:
        p = Path(env)
        if p.exists():
            return p
        raise FileNotFoundError(f"BEEBIUM_SERVER={env} not found")
    repo_root = Path(__file__).parent.parent.parent.parent
    exe_suffix = ".exe" if sys.platform == "win32" else ""
    candidates = [
        repo_root / "build-release" / "src" / "server" / f"beebium-model-b-romram{exe_suffix}",
        repo_root / "build" / "src" / "server" / f"beebium-model-b-romram{exe_suffix}",
        repo_root / "cmake-build-debug" / "src" / "server" / f"beebium-model-b-romram{exe_suffix}",
    ]
    for c in candidates:
        if c.exists():
            return c
    pytest.skip(
        "beebium-model-b-romram not found. Set BEEBIUM_SERVER or build the server."
    )


@pytest.fixture(scope="session")
def l3fs_ssd_filepath():
    """Path to the FS3v126.ssd disc image."""
    env = os.environ.get("L3FS_SSD")
    if env:
        p = Path(env)
        if p.exists():
            return p
        raise FileNotFoundError(f"L3FS_SSD={env} not found")
    repo_root = Path(__file__).parent.parent.parent.parent
    committed = repo_root / "tests" / "assets" / "discs" / "FS3v126.ssd"
    if committed.exists():
        return committed
    pytest.skip("FS3v126.ssd not found. Set L3FS_SSD environment variable.")


@pytest.fixture(scope="session")
def scsi_hdd_filepath():
    """Path to a SCSI HDD image."""
    env = os.environ.get("SCSI_HDD")
    if env:
        p = Path(env)
        if p.exists():
            return p
        raise FileNotFoundError(f"SCSI_HDD={env} not found")
    pytest.skip("No SCSI HDD image. Set the SCSI_HDD environment variable.")
