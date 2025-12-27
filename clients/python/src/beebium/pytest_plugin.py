# Copyright 2025 Robert Smallshire <robert@smallshire.org.uk>
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

"""pytest fixtures for beebium testing.

This module is auto-registered as a pytest plugin via the entry point in pyproject.toml.
Fixtures are available automatically after installing beebium.

Usage:
    def test_basic_print(bbc):
        bbc.keyboard.type("PRINT 42")
        bbc.keyboard.press_return()
"""

from __future__ import annotations

import os
from pathlib import Path

import pytest

from beebium.client import Beebium


def pytest_addoption(parser: pytest.Parser) -> None:
    """Add beebium-specific command line options."""
    group = parser.getgroup("beebium", "Beebium emulator options")
    group.addoption(
        "--beebium-rom-dir",
        action="store",
        default=None,
        help="Directory containing ROM files (default: $BEEBIUM_ROM_DIR)",
    )
    group.addoption(
        "--beebium-server",
        action="store",
        default=None,
        help="Path to beebium-server executable (default: $BEEBIUM_SERVER)",
    )


@pytest.fixture(scope="session")
def beebium_roms_dirpath(request: pytest.FixtureRequest) -> Path:
    """Path to ROM files directory.

    Looks for ROMs in this order:
    1. --beebium-rom-dir command line option
    2. BEEBIUM_ROM_DIR environment variable
    3. Common locations relative to the test file

    Raises:
        pytest.skip: If ROMs cannot be found.
    """
    # 1. Command line option
    cli_path = request.config.getoption("--beebium-rom-dir")
    if cli_path:
        path = Path(cli_path)
        if path.exists():
            return path
        pytest.skip(f"ROM directory not found: {cli_path}")

    # 2. Environment variable
    env_path = os.environ.get("BEEBIUM_ROM_DIR")
    if env_path:
        path = Path(env_path)
        if path.exists():
            return path
        pytest.skip(f"BEEBIUM_ROM_DIR points to non-existent path: {env_path}")

    # 3. Common locations
    candidates = [
        # Relative to this package (in-repo development)
        Path(__file__).parent.parent.parent.parent.parent / "roms",
        # User's home directory
        Path.home() / ".beebium" / "roms",
        # /usr/share location
        Path("/usr/share/beebium/roms"),
    ]

    for candidate in candidates:
        if candidate.exists():
            return candidate

    pytest.skip(
        "ROMs not found. Set BEEBIUM_ROM_DIR environment variable "
        "or use --beebium-rom-dir option."
    )


@pytest.fixture(scope="session")
def mos_filepath(beebium_roms_dirpath: Path) -> Path:
    """Path to MOS ROM file.

    Tries common MOS ROM filenames in the ROM directory.
    """
    candidates = ["acorn-mos_1_20.rom", "OS12.ROM", "os12.rom", "MOS.ROM", "mos.rom", "OS1.2.ROM"]
    for name in candidates:
        path = beebium_roms_dirpath / name
        if path.exists():
            return path

    pytest.skip(
        f"MOS ROM not found in {beebium_roms_dirpath}. "
        f"Expected one of: {', '.join(candidates)}"
    )


@pytest.fixture(scope="session")
def basic_filepath(beebium_roms_dirpath: Path) -> Path | None:
    """Path to BASIC ROM file, or None if not found.

    Tries common BASIC ROM filenames in the ROM directory.
    Returns None (does not skip) if not found - BASIC is optional.
    """
    candidates = ["bbc-basic_2.rom", "BASIC2.ROM", "basic2.rom", "BASIC.ROM", "basic.rom"]
    for name in candidates:
        path = beebium_roms_dirpath / name
        if path.exists():
            return path

    return None


@pytest.fixture(scope="session")
def beebium_server_filepath(request: pytest.FixtureRequest) -> Path | None:
    """Path to beebium-server executable, or None to auto-detect.

    Looks in this order:
    1. --beebium-server command line option
    2. BEEBIUM_SERVER environment variable
    3. None (let ServerProcess auto-detect)
    """
    # 1. Command line option
    cli_path = request.config.getoption("--beebium-server")
    if cli_path:
        path = Path(cli_path)
        if path.exists() and os.access(path, os.X_OK):
            return path
        pytest.skip(f"beebium-server not found or not executable: {cli_path}")

    # 2. Environment variable (will be checked by ServerProcess)
    # 3. Auto-detect (return None)
    return None


@pytest.fixture(scope="function")
def bbc(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
) -> Beebium:
    """A fresh BBC Micro instance for each test.

    The emulator is reset for each test function.

    Usage:
        def test_print(bbc):
            bbc.debugger.stop()
            bbc.keyboard.type("PRINT 42")
            bbc.keyboard.press_return()
    """
    with Beebium.launch(
        mos_filepath=mos_filepath,
        basic_filepath=basic_filepath,
        server_filepath=beebium_server_filepath,
    ) as instance:
        yield instance


@pytest.fixture(scope="module")
def bbc_shared(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
) -> Beebium:
    """A BBC Micro instance shared across tests in a module.

    Use when tests need to build on each other's state.
    The emulator runs continuously for the module.

    Usage:
        def test_load_program(bbc_shared):
            bbc_shared.memory.load(0x1900, "mygame.bin")

        def test_run_program(bbc_shared):
            # Assumes previous test loaded the program
            bbc_shared.debugger.run_until(0x1900)
    """
    with Beebium.launch(
        mos_filepath=mos_filepath,
        basic_filepath=basic_filepath,
        server_filepath=beebium_server_filepath,
    ) as instance:
        yield instance


@pytest.fixture
def stopped_bbc(bbc: Beebium) -> Beebium:
    """A BBC Micro that starts in stopped state.

    Convenience fixture that stops the emulator before yielding.
    Resumes on cleanup if still stopped.
    """
    bbc.debugger.stop()
    yield bbc
    # Resume on cleanup to avoid blocking
    if bbc.debugger.is_stopped:
        try:
            bbc.debugger.run()
        except Exception:
            pass  # Ignore errors during cleanup
