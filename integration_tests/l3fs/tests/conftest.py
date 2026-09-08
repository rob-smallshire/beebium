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

"""Pytest fixtures for Level 3 File Server Econet integration tests.

These tests launch two Beebium instances:
- A file server (station 254) running the L3FS v1.26 on a Model B with Tube,
  SCSI, ADFS, ANFS, and RTC
- A client station (station 221) running a Model B with ANFS only

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
DISCS_ASSETS_DIRPATH = REPO_ROOT / "tests" / "assets" / "discs"

# Source SSD floppy image containing the L3FS v1.26 binary, used to build
# the SCSI hard disc image on demand via the oaknut `disc` CLI.
FS3V126_SSD = DISCS_ASSETS_DIRPATH / "FS3v126.ssd"


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
    """Path to the beebium-model-b server executable."""
    env = os.environ.get("BEEBIUM_SERVER")
    if env:
        p = Path(env)
        if p.exists():
            return p
        raise FileNotFoundError(f"BEEBIUM_SERVER={env} not found")
    exe_suffix = ".exe" if sys.platform == "win32" else ""
    candidates = [
        REPO_ROOT / "build-release" / "src" / "server" / f"beebium-model-b{exe_suffix}",
        REPO_ROOT / "build" / "src" / "server" / f"beebium-model-b{exe_suffix}",
        REPO_ROOT / "cmake-build-debug" / "src" / "server" / f"beebium-model-b{exe_suffix}",
    ]
    for c in candidates:
        if c.exists():
            return c
    pytest.skip(
        "beebium-model-b not found. Set BEEBIUM_SERVER or build the server."
    )


@pytest.fixture(scope="session")
def anfs_filepath(roms_dirpath):
    """Path to the ANFS 4.18 ROM (provides Tube Host Code + NFS)."""
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


# ---- Function-scope fixtures ----


def _run_disc(*args, cwd=None, input=None):
    """Invoke the oaknut `disc` CLI via its console script."""
    result = subprocess.run(
        ["disc", *args],
        cwd=cwd,
        input=input,
        capture_output=True,
        text=isinstance(input, str) or input is None,
        check=False,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"`disc {' '.join(str(a) for a in args)}` failed "
            f"(exit {result.returncode}):\n"
            f"stdout: {result.stdout!r}\nstderr: {result.stderr!r}"
        )
    return result


@pytest.fixture
def l3fs_scsi_filepath(tmp_path):
    """Build a fresh L3FS SCSI hard disc image in tmp_path.

    Each test gets a freshly-built disc so file-server writes from one
    test do not contaminate the next. The disc is constructed on the
    fly using the oaknut `disc` CLI (a dependency of this test
    project) from the L3FS v1.26 binary in tests/assets/discs/.

    Returns the path to the .dat file; the matching .dsc descriptor is
    created alongside it.

    Build steps (matching the recipe in
    docs/level-3-file-server-setup.md):

    1.  Create a 10 MB ADFS hard disc image titled "Server".
    2.  Copy $.FS3v126 from the source SSD into the new image.
    3.  Write an !BOOT file that does *ADFS then *RUN $.FS3v126.
    4.  Set the boot option to EXEC.
    5.  Initialise an AFS partition on the disc:
          - disc name "L3DATA"
          - one user "USER1" with a 2 MB quota
          - omit the default "Welcome" user
          - emplace the standard Library and Library1 directories.

    The standard SYST and BOOT users are created automatically.
    """
    if not FS3V126_SSD.exists():
        pytest.skip(
            f"L3FS source floppy not found: {FS3V126_SSD}. "
            "Add the FS3v126.ssd L3FS v1.26 binary to tests/assets/discs/."
        )

    dat = tmp_path / "l3fs.dat"
    src_ssd = tmp_path / "FS3v126.ssd"
    shutil.copy2(FS3V126_SSD, src_ssd)

    # 1. Create a 10 MB ADFS-formatted hard disc.
    _run_disc(
        "create", str(dat),
        "--geometry", "capacity=10MB",
        "--title", "Server",
    )

    # 2. Copy the file-server binary onto the new disc.
    _run_disc(
        "cp",
        f"{src_ssd}:$.FS3v126",
        f"{dat}:$.FS3v126",
    )

    # 3. Write the !BOOT file (binary input -- bytes, not text).
    _run_disc(
        "put", f"{dat}:$.!BOOT", "-",
        input=b"*ADFS\r*RUN $.FS3v126\r",
    )

    # 4. Set the disc boot option to EXEC.
    _run_disc("opt", str(dat), "EXEC")

    # 5. Initialise the AFS partition with the standard layout.
    _run_disc(
        "afs-init", str(dat),
        "--disc-name", "L3DATA",
        "--user", "USER1:2MB",
        "--omit-user", "Welcome",
        "--emplace", "Library",
        "--emplace", "Library1",
    )

    return dat
