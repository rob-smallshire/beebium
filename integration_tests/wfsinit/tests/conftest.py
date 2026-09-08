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

"""Pytest fixtures for WFSINIT diagnostic integration tests.

These tests boot a BBC Model B with ROM/RAM board, Tube 65C02, SCSI hard disc,
and DFS floppy to run the Acorn WFSINIT utility and diagnose where it hangs.

Tests are long-running and require external resources (ROMs, disc images).
They are disabled by default and must be run with ``pytest -m slow``.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest


# Path to the repo root (four levels up from this file).
REPO_ROOT = Path(__file__).parent.parent.parent.parent

# Pre-built disc image assets.
SCSI_ASSETS_DIRPATH = REPO_ROOT / "tests" / "assets" / "scsi"


def pytest_collection_modifyitems(config, items):
    """Skip tests marked 'slow' unless explicitly requested."""
    if config.getoption("-m", default="") and "slow" in config.getoption("-m"):
        return
    skip_slow = pytest.mark.skip(reason="slow test -- run with pytest -m slow")
    for item in items:
        if "slow" in item.keywords:
            item.add_marker(skip_slow)


# ---- Session-scope fixtures ----


@pytest.fixture(scope="session")
def roms_dirpath():
    """Path to the ROM directory."""
    env = os.environ.get("BEEBIUM_ROM_DIR")
    if env:
        p = Path(env)
        if p.is_dir():
            return p
        raise FileNotFoundError(f"BEEBIUM_ROM_DIR={env} is not a directory")
    candidates = [REPO_ROOT / "roms"]
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
    exe_suffix = ".exe" if sys.platform == "win32" else ""
    candidates = [
        REPO_ROOT / "build-release" / "src" / "server" / f"beebium-model-b-romram{exe_suffix}",
        REPO_ROOT / "build" / "src" / "server" / f"beebium-model-b-romram{exe_suffix}",
        REPO_ROOT / "cmake-build-debug" / "src" / "server" / f"beebium-model-b-romram{exe_suffix}",
    ]
    for c in candidates:
        if c.exists():
            return c
    pytest.skip(
        "beebium-model-b-romram not found. Set BEEBIUM_SERVER or build the server."
    )


@pytest.fixture(scope="session")
def anfs_filepath(roms_dirpath):
    """Path to the ANFS 4.18 ROM (provides Tube Host Code)."""
    p = roms_dirpath / "acorn-anfs_4_18.rom"
    if not p.exists():
        pytest.skip(f"ANFS 4.18 ROM not found: {p}")
    return p


@pytest.fixture(scope="session")
def adfs_filepath(roms_dirpath):
    """Path to the ADFS 1.30 ROM."""
    p = roms_dirpath / "acorn-adfs_1_30.rom"
    if not p.exists():
        pytest.skip(f"ADFS 1.30 ROM not found: {p}")
    return p


@pytest.fixture(scope="session")
def dfs_filepath(roms_dirpath):
    """Path to the DFS 2.26 ROM (1770-compatible)."""
    p = roms_dirpath / "acorn-dfs_2_26.rom"
    if not p.exists():
        pytest.skip(f"DFS 2.26 ROM not found: {p}")
    return p


@pytest.fixture(scope="session")
def wfsinit_ssd_filepath():
    """Path to the WFSINIT SSD floppy image (read-only)."""
    p = SCSI_ASSETS_DIRPATH / "wfsinit.ssd"
    if not p.exists():
        pytest.skip(f"WFSINIT SSD not found: {p}")
    return p


# ---- Function-scope fixtures ----


@pytest.fixture
def scsi_hdd_filepath(tmp_path):
    """Build a fresh 20MB ADFS hard-disc image (DAT + DSC) using oaknut's `disc` CLI.

    WFSINIT writes to the disc, so each test run gets its own blank image.
    Generating on the fly avoids checking a 20MB binary into Git.
    """
    disc_cli_filepath = shutil.which("disc")
    if disc_cli_filepath is None:
        pytest.skip("oaknut `disc` CLI not on PATH (run under `uv run`, or `uv sync` this project)")

    dat_filepath = tmp_path / "l3fs-blank-20mb.dat"
    result = subprocess.run(
        [
            disc_cli_filepath, "create", str(dat_filepath),
            "--geometry", "capacity=20MB",
            "--title", "Server",
        ],
        capture_output=True, text=True,
    )
    if result.returncode != 0:
        pytest.fail(
            f"`disc create` failed (exit {result.returncode}):\n"
            f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )

    dsc_filepath = tmp_path / "l3fs-blank-20mb.dsc"
    assert dat_filepath.exists(), f"disc create did not produce {dat_filepath}"
    assert dsc_filepath.exists(), f"disc create did not produce {dsc_filepath}"
    return dat_filepath
