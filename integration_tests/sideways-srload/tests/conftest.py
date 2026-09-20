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

"""Pytest fixtures for the *SRLOAD live-header-monitoring integration test.

Builds a fresh DFS SSD carrying the ANFS ROM as a file named ``R.ANFS``,
boots a Model B with the ROM/RAM expansion board, BASIC at slot 15,
DFS 2.26 at slot 14, and slot 7 forced to RAM so ``*SRLOAD`` has
somewhere to land. DFS 2.26 carries the SRAM utilities (``*SRLOAD``,
``*SRWRITE``, ``*SRSAVE``, ``*SRDATA``, ``*SRROM``, ``*SRREAD``); this
is exactly the user-facing path the live header monitor exists to
surface.

The Model B+ would be the more natural choice on real hardware (its
integral DFS is 2.26), but our B+ memory model has no sideways RAM
support - all six ROM sockets are fixed Rom<16384>. The ROM/RAM board
variant emulates the third-party hardware that adds configurable
sideways RAM to a Model B and is the smallest emulator surface
that exercises this code path end to end.
"""

from __future__ import annotations

import os
from pathlib import Path

import pytest

from oaknut.dfs import DFS, ACORN_DFS_40T_SINGLE_SIDED

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.screen import screen_contains, dump_screen


# DFS 2.26 carries the SRAM utilities (*SRLOAD &c). We load it
# explicitly rather than relying on a preset, so the test stays correct
# if the model-b-romram preset wiring ever shifts.
DFS_ROM_FILENAME_ROM = "acorn-dfs_2_26.rom"
BASIC_ROM_FILENAME_ROM = "bbc-basic_2.rom"
MOS_ROM_FILENAME = "acorn-mos_1_20.rom"

# The ROM image we'll *SRLOAD off the disc into sideways RAM.
SIDEWAYS_ROM_FILENAME = "acorn-anfs_4_18.rom"

# Slot 7 is one of the ROM/RAM board's independent slots. We configure
# it as RAM so *SRLOAD ... 7 has somewhere to write to.
SRAM_TARGET_SLOT = 7

# DFS filename for the loaded ROM. The "R." prefix is the conventional
# DFS naming for ROM images.
DFS_ROM_FILENAME = "R.ANFS"

SSD_SIZE = 102400  # 40 tracks * 10 sectors * 256 bytes


# ---- Path discovery -------------------------------------------------------

@pytest.fixture(scope="session")
def romram_server_filepath() -> Path:
    """Path to the beebium-model-b-romram executable.

    Looks first at $BEEBIUM_MODEL_B_ROMRAM_SERVER, then a sibling of
    $BEEBIUM_SERVER, then $BEEBIUM_SERVER_DIR.
    """
    env = os.environ.get("BEEBIUM_MODEL_B_ROMRAM_SERVER")
    if env:
        path = Path(env)
        if path.exists():
            return path
        pytest.skip(f"BEEBIUM_MODEL_B_ROMRAM_SERVER set but not found: {path}")

    server_dirpath = os.environ.get("BEEBIUM_SERVER_DIR")
    if server_dirpath:
        path = Path(server_dirpath) / "beebium-model-b-romram"
        if path.exists():
            return path

    model_b = os.environ.get("BEEBIUM_SERVER")
    if model_b:
        path = Path(model_b).parent / "beebium-model-b-romram"
        if path.exists():
            return path

    pytest.skip(
        "beebium-model-b-romram not found. Set BEEBIUM_MODEL_B_ROMRAM_SERVER "
        "or BEEBIUM_SERVER_DIR (build/src/server)."
    )


@pytest.fixture(scope="session")
def mos_rom_filepath(beebium_roms_dirpath: Path) -> Path:
    """Path to the Model B MOS ROM."""
    path = beebium_roms_dirpath / MOS_ROM_FILENAME
    if not path.exists():
        pytest.skip(f"MOS not found: {path}")
    return path


@pytest.fixture(scope="session")
def sideways_rom_filepath(beebium_roms_dirpath: Path) -> Path:
    """Path to the ANFS ROM image - what we'll *SRLOAD into sideways RAM."""
    path = beebium_roms_dirpath / SIDEWAYS_ROM_FILENAME
    if not path.exists():
        pytest.skip(f"ANFS ROM not found: {path}")
    return path


@pytest.fixture(scope="session")
def dfs_rom_filepath(beebium_roms_dirpath: Path) -> Path:
    """Path to the 1770 DFS ROM (2.26 - has the SRAM utilities)."""
    path = beebium_roms_dirpath / DFS_ROM_FILENAME_ROM
    if not path.exists():
        pytest.skip(f"1770 DFS ROM not found: {path}")
    return path


@pytest.fixture(scope="session")
def basic_rom_filepath(beebium_roms_dirpath: Path) -> Path:
    """Path to the BASIC ROM."""
    path = beebium_roms_dirpath / BASIC_ROM_FILENAME_ROM
    if not path.exists():
        pytest.skip(f"BASIC ROM not found: {path}")
    return path


# ---- Disc image -----------------------------------------------------------

@pytest.fixture
def srload_ssd_filepath(tmp_path: Path, sideways_rom_filepath: Path) -> Path:
    """Fresh DFS SSD carrying R.ANFS as a single 16 KiB file."""
    rom_bytes = sideways_rom_filepath.read_bytes()
    buffer = bytearray(SSD_SIZE)
    dfs = DFS.from_buffer(memoryview(buffer), ACORN_DFS_40T_SINGLE_SIDED)
    # Load/exec addresses are irrelevant for *SRLOAD - it reads the file
    # bytes and writes them straight into the named sideways bank.
    # In DFS the dot is the directory separator, not part of the
    # filename: "R.ANFS" means file ANFS in directory R. The DFS *SRLOAD
    # command resolves this path correctly.
    dfs.path(DFS_ROM_FILENAME).write_bytes(
        rom_bytes,
        load_address=0xFFFF8000,
        exec_address=0xFFFF8000,
    )
    filepath = tmp_path / "srload.ssd"
    filepath.write_bytes(bytes(buffer))
    return filepath


# ---- Emulator -------------------------------------------------------------

@pytest.fixture(scope="session")
def adfs_rom_filepath(beebium_roms_dirpath: Path) -> Path:
    """Path to the ADFS ROM image."""
    path = beebium_roms_dirpath / "acorn-adfs_1_30.rom"
    if not path.exists():
        pytest.skip(f"ADFS ROM not found: {path}")
    return path


@pytest.fixture(scope="session")
def amx_rom_filepath(beebium_roms_dirpath: Path) -> Path:
    """AMX Super Rom 3.41 - a service ROM with no hardware probes.

    Responds to *HELP with "AMX Super Rom V3.41 / By David Elliot",
    making it a clean probe for whether *SRLOAD-then-Break delivers
    a usable service ROM, without the confounding hardware checks
    that ADFS performs.
    """
    path = beebium_roms_dirpath / "amx-super-rom_3_41.rom"
    if not path.exists():
        pytest.skip(f"AMX Super Rom not found: {path}")
    return path


@pytest.fixture(scope="session")
def comal_rom_filepath(beebium_roms_dirpath: Path) -> Path:
    """Acornsoft COMAL 1.0 - a language ROM that reports "COMAL" via
    *HELP and has no hardware probes. Complements the AMX probe by
    testing whether a *language* ROM (rather than a service ROM)
    survives *SRLOAD-into-RAM intact.
    """
    path = beebium_roms_dirpath / "comal_1_0.rom"
    if not path.exists():
        pytest.skip(f"Acornsoft COMAL not found: {path}")
    return path


@pytest.fixture
def srload_ssd_with_adfs_filepath(
    tmp_path: Path,
    adfs_rom_filepath: Path,
    amx_rom_filepath: Path,
    comal_rom_filepath: Path,
) -> Path:
    """Fresh DFS SSD carrying R.ADFS, R.AMX, and R.COMAL.

    AMX (service ROM, no hardware probes) and COMAL (language ROM,
    no hardware probes) are clean probes for the *SRLOAD-into-RAM
    path. ADFS is the bug-reproducer ROM whose self-test fails.
    """
    buffer = bytearray(SSD_SIZE)
    dfs = DFS.from_buffer(memoryview(buffer), ACORN_DFS_40T_SINGLE_SIDED)
    dfs.path("R.ADFS").write_bytes(
        adfs_rom_filepath.read_bytes(),
        load_address=0xFFFF8000,
        exec_address=0xFFFF8000,
    )
    dfs.path("R.AMX").write_bytes(
        amx_rom_filepath.read_bytes(),
        load_address=0xFFFF8000,
        exec_address=0xFFFF8000,
    )
    dfs.path("R.COMAL").write_bytes(
        comal_rom_filepath.read_bytes(),
        load_address=0xFFFF8000,
        exec_address=0xFFFF8000,
    )
    filepath = tmp_path / "srload-adfs.ssd"
    filepath.write_bytes(bytes(buffer))
    return filepath


# ---- B+ 128K emulator -----------------------------------------------------

@pytest.fixture(scope="session")
def b_plus_128k_server_filepath() -> Path:
    """Path to the beebium-model-b-plus-128k executable."""
    env = os.environ.get("BEEBIUM_MODEL_B_PLUS_128K_SERVER")
    if env:
        path = Path(env)
        if path.exists():
            return path
        pytest.skip(f"BEEBIUM_MODEL_B_PLUS_128K_SERVER set but not found: {path}")

    server_dirpath = os.environ.get("BEEBIUM_SERVER_DIR")
    if server_dirpath:
        path = Path(server_dirpath) / "beebium-model-b-plus-128k"
        if path.exists():
            return path

    model_b = os.environ.get("BEEBIUM_SERVER")
    if model_b:
        path = Path(model_b).parent / "beebium-model-b-plus-128k"
        if path.exists():
            return path

    pytest.skip(
        "beebium-model-b-plus-128k not found. Set "
        "BEEBIUM_MODEL_B_PLUS_128K_SERVER or BEEBIUM_SERVER_DIR."
    )


@pytest.fixture(scope="session")
def b_plus_mos_rom_filepath(beebium_roms_dirpath: Path) -> Path:
    """The Model B+ MOS (different image from Model B's)."""
    path = beebium_roms_dirpath / "acorn-mos_2_0.rom"
    if not path.exists():
        pytest.skip(f"B+ MOS not found: {path}")
    return path


@pytest.fixture
def b_plus_128k_with_adfs_at_startup(
    b_plus_mos_rom_filepath: Path,
    b_plus_128k_server_filepath: Path,
    adfs_rom_filepath: Path,
):
    """B+ 128K with ADFS loaded at startup into IC62 high (slot 9).

    Bypasses *SRLOAD entirely - ADFS is preloaded into a regular ROM
    socket via --sideways. If ADFS boots cleanly here but not after
    *SRLOAD, the issue is in the SRLOAD path or in how SRAM banks
    behave. If it fails here too, ADFS itself has a problem in our
    B+ emulation.
    """
    try:
        with Beebium.launch(
            mos_filepath=b_plus_mos_rom_filepath,
            basic_filepath=None,
            server_filepath=b_plus_128k_server_filepath,
            extra_args=[
                "--sideways", f"slot=9:type=rom:image={adfs_rom_filepath}",
            ],
            startup_timeout=15.0,
        ) as bbc:
            ok = bbc.run_until_or_timeout(
                lambda: screen_contains(bbc, ">"),
                emulated_seconds=10.0,
            )
            if not ok:
                pytest.fail(
                    "B+ 128K with ADFS at slot 9 failed to reach BASIC prompt:\n"
                    f"{dump_screen(bbc)}"
                )
            yield bbc
    except ServerNotFoundError as exc:
        pytest.skip(str(exc))


@pytest.fixture
def b_plus_128k_with_adfs_disc(
    b_plus_mos_rom_filepath: Path,
    b_plus_128k_server_filepath: Path,
    srload_ssd_with_adfs_filepath: Path,
):
    """B+ 128K booted with an SSD carrying R.ADFS in floppy 0.

    BASIC + DFS 2.26 load from the B+ defaults. Slot 12 (SRAM W) is
    integral RAM on this variant, so no override is needed.
    """
    try:
        with Beebium.launch(
            mos_filepath=b_plus_mos_rom_filepath,
            basic_filepath=None,
            server_filepath=b_plus_128k_server_filepath,
            extra_args=[
                "--floppy", f"0:{srload_ssd_with_adfs_filepath}",
            ],
            startup_timeout=15.0,
        ) as bbc:
            ok = bbc.run_until_or_timeout(
                lambda: screen_contains(bbc, ">"),
                emulated_seconds=10.0,
            )
            if not ok:
                pytest.fail(
                    "B+ 128K failed to reach BASIC prompt:\n"
                    f"{dump_screen(bbc)}"
                )
            yield bbc
    except ServerNotFoundError as exc:
        pytest.skip(str(exc))


@pytest.fixture
def romram_with_srload_disc(
    mos_rom_filepath: Path,
    basic_rom_filepath: Path,
    dfs_rom_filepath: Path,
    romram_server_filepath: Path,
    srload_ssd_filepath: Path,
):
    """Model B with ROM/RAM board booted with the SRLOAD disc.

    Yields a connected Beebium client at the BASIC ``>`` prompt with:
      - BASIC at slot 15 (language ROM)
      - DFS 2.26 at slot 14 (carries the SRAM utilities)
      - Slot 7 configured as sideways RAM (the *SRLOAD target)
      - Floppy 0 mounted with R.ANFS on a fresh DFS image
    """
    try:
        with Beebium.launch(
            mos_filepath=mos_rom_filepath,
            basic_filepath=basic_rom_filepath,
            server_filepath=romram_server_filepath,
            extra_args=[
                "--fdc", "acorn-1770",
                "--sideways", f"slot=14:type=rom:image={dfs_rom_filepath}",
                "--sideways", f"slot={SRAM_TARGET_SLOT}:type=ram",
                "--floppy", f"0:{srload_ssd_filepath}",
            ],
            startup_timeout=15.0,
        ) as bbc:
            ok = bbc.run_until_or_timeout(
                lambda: screen_contains(bbc, ">"),
                emulated_seconds=10.0,
            )
            if not ok:
                pytest.fail(
                    "Machine failed to reach BASIC prompt:\n"
                    f"{dump_screen(bbc)}"
                )
            yield bbc
    except ServerNotFoundError as exc:
        pytest.skip(str(exc))
