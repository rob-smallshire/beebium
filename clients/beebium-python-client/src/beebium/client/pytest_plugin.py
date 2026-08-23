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
from collections.abc import Iterator
from pathlib import Path

import grpc
import pytest

from beebium.client import Beebium
from beebium.client.exceptions import BeebiumError, ServerNotFoundError
from beebium.client.server import ServerProcess


def _find_checkout_roms(rootpath: Path) -> Path | None:
    """Find the repo's roms/ directory at or above pytest's rootdir.

    When beebium is installed from a wheel its __file__ is in site-packages
    with no checkout above it, but pytest's rootdir still points into the
    checkout under test (tests/ live there), so it is the reliable anchor.
    """
    for ancestor in (rootpath, *rootpath.parents):
        candidate = ancestor / "roms"
        if candidate.is_dir():
            return candidate
    return None


def _find_checkout_server(rootpath: Path) -> Path | None:
    """Find the freshly-built server in a build directory at or above rootdir.

    Uses the same anchor as _find_checkout_roms so a wheel-installed client
    still finds the checkout's own server rather than one on PATH.
    """
    exe_name = ServerProcess._exe_name("beebium-model-b")
    return ServerProcess._find_in_repo_build(exe_name, [rootpath])


def _wheel_rom_dirpath() -> Path | None:
    """The installed beebium-server wheel's ROM directory, if importable."""
    try:
        import beebium.server  # lazy: never import at module load
    except ImportError:
        return None
    romdir = Path(beebium.server.rom_dirpath())
    return romdir if romdir.is_dir() else None


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
        help="Server to use: a beebium-model-b binary or an install root "
        "(default: $BEEBIUM_SERVER, then the checkout build / wheel / PATH)",
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

    # 3. The checkout's own roms/, located from pytest's rootdir. This works
    #    whether beebium is installed from a wheel or as an editable checkout.
    checkout_roms = _find_checkout_roms(Path(request.config.rootpath))
    if checkout_roms is not None:
        return checkout_roms

    # 4. The installed beebium-server wheel's ROMs (present when that package is
    #    installed), after the checkout so a development run prefers its own.
    wheel_roms = _wheel_rom_dirpath()
    if wheel_roms is not None:
        return wheel_roms

    # 5. Common install locations
    candidates = [
        # User's home directory
        Path.home() / ".beebium" / "roms",
        # /usr/share location
        Path("/usr/share/beebium/roms"),
    ]

    for candidate in candidates:
        if candidate.exists():
            return candidate

    pytest.skip("ROMs not found. Set BEEBIUM_ROM_DIR environment variable or use --beebium-rom-dir option.")


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

    pytest.skip(f"MOS ROM not found in {beebium_roms_dirpath}. Expected one of: {', '.join(candidates)}")


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
    2. BEEBIUM_SERVER environment variable (deferred to ServerProcess)
    3. The checkout's own build, located from pytest's rootdir
    4. None (let ServerProcess auto-detect: its __file__ walk, then PATH)
    """
    # 1. Command line option -- a binary or an install root. Return it as-is;
    #    ServerProcess (via server=) coerces and validates it precisely.
    cli_path = request.config.getoption("--beebium-server")
    if cli_path:
        path = Path(cli_path)
        if path.exists():
            return path
        pytest.skip(f"--beebium-server path does not exist: {cli_path}")

    # 2. Environment variable -- defer to ServerProcess, which validates it and
    #    fails loudly if it is wrong. CI sets this, so it must keep priority.
    if os.environ.get("BEEBIUM_SERVER"):
        return None

    # 3. The checkout's own build, located from pytest's rootdir. Returned as an
    #    explicit path so it outranks a stray server on PATH -- e.g. a system-
    #    installed one built from a different protocol, which would otherwise be
    #    chosen and then fail the fingerprint handshake with an opaque error.
    #    A wheel-installed client cannot find this via its own __file__ (it lives
    #    in site-packages), so the plugin locates it from rootdir instead.
    checkout_server = _find_checkout_server(Path(request.config.rootpath))
    if checkout_server is not None:
        return checkout_server

    # 4. Auto-detect (return None)
    return None


@pytest.fixture(scope="function")
def bbc(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
) -> Iterator[Beebium]:
    """A fresh BBC Micro instance for each test.

    The emulator is reset for each test function.
    Skips the test if the server executable is not available.

    Usage:
        def test_print(bbc):
            bbc.debugger.stop()
            bbc.keyboard.type("PRINT 42")
            bbc.keyboard.press_return()
    """
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server=beebium_server_filepath,
        ) as instance:
            yield instance
    except ServerNotFoundError as e:
        pytest.skip(str(e))


@pytest.fixture(scope="module")
def bbc_shared(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
) -> Iterator[Beebium]:
    """A BBC Micro instance shared across tests in a module.

    Use when tests need to build on each other's state.
    The emulator runs continuously for the module.
    Skips the test if the server executable is not available.

    Usage:
        def test_load_program(bbc_shared):
            bbc_shared.memory.load(0x1900, "mygame.bin")

        def test_run_program(bbc_shared):
            # Assumes previous test loaded the program
            bbc_shared.debugger.run_to(0x1900)
    """
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server=beebium_server_filepath,
        ) as instance:
            yield instance
    except ServerNotFoundError as e:
        pytest.skip(str(e))


@pytest.fixture
def stopped_bbc(bbc: Beebium) -> Iterator[Beebium]:
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
        except (BeebiumError, grpc.RpcError):
            pass  # Ignore errors during cleanup


@pytest.fixture(scope="function")
def bbc_tube(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
) -> Iterator[Beebium]:
    """A BBC Micro instance with Tube coprocessor enabled.

    Launches the server with --tube 65C02-3MHz and uses a longer
    startup timeout to allow the parasite process to connect.

    Usage:
        def test_tube_feature(bbc_tube):
            status = bbc_tube.tube.status
            assert status.parasite_connected
    """
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server=beebium_server_filepath,
            extra_args=["--tube", "65C02-3MHz"],
            startup_timeout=20.0,
        ) as instance:
            yield instance
    except ServerNotFoundError as e:
        pytest.skip(str(e))
