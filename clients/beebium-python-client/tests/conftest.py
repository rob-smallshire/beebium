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

"""pytest configuration for beebium Python client tests.

The beebium pytest fixtures are auto-registered via the entry point in pyproject.toml.
This conftest.py adds additional test-specific fixtures shared across test files.
"""

from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError

from firetrack import FIRETRACK_DISC_FILENAME
from prestel_helpers import COMMSTAR_ROM_FILENAME, enter_prestel_chat
from tube_test_helpers import DFS_1770_ROM_CANDIDATES, find_dfs_1770_rom

# The beebium fixtures (bbc, bbc_shared, stopped_bbc, etc.) are automatically
# available from the beebium.client.pytest_plugin module via the entry point.


@pytest.fixture(scope="module")
def dfs_1770_rom_filepath(beebium_roms_dirpath: Path) -> Path:
    """Path to a 1770 DFS ROM file."""
    path = find_dfs_1770_rom(beebium_roms_dirpath)
    if path is None:
        pytest.skip(f"1770 DFS ROM not found. Expected one of: {', '.join(DFS_1770_ROM_CANDIDATES)}")
    return path


@pytest.fixture(scope="module")
def firetrack_disc_filepath() -> Path:
    """Path to the committed Firetrack disc image (a shared test fixture)."""
    repo_root = Path(__file__).parent.parent.parent.parent
    path = repo_root / "tests" / "assets" / "discs" / FIRETRACK_DISC_FILENAME
    if not path.exists():
        pytest.skip(f"Firetrack disc image not found: {path}")
    return path


@pytest.fixture
def bbc_firetrack(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
    dfs_1770_rom_filepath: Path,
    firetrack_disc_filepath: Path,
) -> Beebium:
    """A BBC Micro that auto-boots the Firetrack disc to its instruction story.

    Shared across Firetrack tests; the navigation from here (story -> loader ->
    custom-SAA5050 intro, or on into the game) lives in the firetrack helper
    module. Left running so a test can drive it in real time.
    """
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=beebium_server_filepath,
            extra_args=[
                "--fdc",
                "acorn-1770",
                "--sideways",
                f"14:rom:{dfs_1770_rom_filepath}",
                "--auto-boot",
                "--floppy",
                f"0:{firetrack_disc_filepath}",
            ],
            startup_timeout=20.0,
        ) as bbc:
            bbc.debugger.ensure_running()
            yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))


# ---------------------------------------------------------------------------
# Pace Commstar: shared fixture and navigation helpers
#
# Commstar is period comms software driving the real serial path, which makes
# it a useful oracle for the ACIA, the Serial ULA at viewdata's 7E1 word
# format, the keyboard matrix and the MOS keyboard scan. Both Commstar test
# modules drive it the same way, so the navigation lives here.
# ---------------------------------------------------------------------------

# The Commstar/Prestel constants and navigation helpers (screen, wait_for_screen,
# enter_prestel_chat, transmitted, and the timing/byte constants) live in
# prestel_helpers.py so the two Commstar test modules can import them from a
# uniquely named module rather than from this conftest -- see that module's
# docstring for why importing a conftest as `from conftest import ...` is unsafe.


@pytest.fixture(scope="module")
def commstar_rom_filepath(beebium_roms_dirpath: Path) -> Path:
    """Path to the Commstar comms ROM."""
    path = beebium_roms_dirpath / COMMSTAR_ROM_FILENAME
    if not path.exists():
        pytest.skip(f"Commstar ROM not found: {path}")
    return path


@pytest.fixture
def commstar_prestel_bbc(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
    commstar_rom_filepath: Path,
):
    """A BBC running Commstar in Prestel chat mode, its serial port on rpc-serial."""
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=beebium_server_filepath,
            extra_args=[
                "--rpc-serial",
                "--sideways",
                f"13:rom:{commstar_rom_filepath}",
            ],
        ) as bbc:
            enter_prestel_chat(bbc)
            yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))
