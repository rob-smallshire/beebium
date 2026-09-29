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

Fixtures:
    launch_bbc: a factory. ``launch_bbc(preset=..., variant=..., extra_args=...,
        startup_timeout=...)`` launches a machine and returns it connected; every
        machine a test launches is torn down when the test ends. A test can
        launch several, of different variants or presets, or launch one only
        after its own setup (building a disc image, say).
    bbc: one machine per test, launched through ``launch_bbc`` with the
        ``beebium_preset`` and ``beebium_extra_args`` defaults.
    bbc_shared: one machine per module, configured the same way as ``bbc``.
    stopped_bbc: ``bbc``, stopped before the test runs.
    bbc_tube: a Model B with a disc filing system and a 65C02 second
        processor on the Tube.

Configuring ``bbc`` and ``bbc_shared``, highest precedence first:
    1. Override ``beebium_preset`` (a preset id or a preset file path) or
       ``beebium_extra_args`` (extra server arguments) in a ``conftest.py`` or
       test module. Keep ``scope="session"`` if ``bbc_shared`` uses them::

           @pytest.fixture(scope="session")
           def beebium_preset():
               return "model-b-disc"

    2. ``--beebium-preset <id-or-path>`` on the command line.
    3. ``beebium_preset = <id-or-path>`` in the pytest ini configuration.
    4. Otherwise no preset: a bare Model B with no disc controller.

A preset is given as an existing file path or as the id ``list-presets``
reports; :func:`resolve_preset` turns an id into its file.
"""

from __future__ import annotations

import contextlib
import os
import subprocess
from collections.abc import Callable, Iterator, Sequence
from pathlib import Path

import grpc
import pytest

from beebium.client import Beebium
from beebium.client.exceptions import BeebiumError, ServerNotFoundError
from beebium.client.installation import DEFAULT_VARIANT, ServerInstallation
from beebium.client.server import ServerProcess

#: File name suffix of a preset file.
PRESET_EXTENSION = ".preset.beebium"

#: The type of the ``launch_bbc`` factory.
LaunchBbc = Callable[..., Beebium]


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
    group.addoption(
        "--beebium-preset",
        action="store",
        default=None,
        help="Preset for the bbc and bbc_shared fixtures: an id such as "
        "model-b-disc, or a preset file path (default: the beebium_preset ini "
        "value, else none)",
    )
    parser.addini(
        "beebium_preset",
        help="Preset for the bbc and bbc_shared fixtures: an id or a preset file path",
        default=None,
    )


def _preset_search_dirpaths(executable_filepath: Path, installation: ServerInstallation) -> list[Path]:
    """Where the server looks for a preset id, in the order it looks.

    Mirrors the server's own search: the system presets (BEEBIUM_SERVERS_DIRPATH,
    then beside the binary as in a build tree, then the installed
    ``share/beebium/presets``), then the user presets directory, which the
    server reports itself so its per-platform rules are not duplicated here.
    """
    real_dirpath = executable_filepath.resolve().parent
    candidates: list[Path | None] = []
    servers_dirpath = os.environ.get("BEEBIUM_SERVERS_DIRPATH")
    if servers_dirpath:
        candidates.append(Path(servers_dirpath) / "presets")
    candidates += [
        installation.preset_dirpath,
        real_dirpath / "presets",
        real_dirpath.parent / "share" / "beebium" / "presets",
    ]
    try:
        reported = subprocess.run(
            [str(executable_filepath), "report-presets-dirpath"],
            capture_output=True,
            text=True,
            timeout=30,
            check=True,
        ).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        reported = ""
    if reported:
        candidates.append(Path(reported.splitlines()[-1]))

    dirpaths: list[Path] = []
    for candidate in candidates:
        if candidate is not None and candidate.is_dir() and candidate not in dirpaths:
            dirpaths.append(candidate)
    return dirpaths


def resolve_preset(
    preset: str | Path,
    server: ServerInstallation | str | Path | None = None,
    variant: str = DEFAULT_VARIANT,
) -> Path:
    """Turn a preset id or path into the preset file to pass as ``--preset``.

    An existing file is returned as it is. Anything else is taken as a preset
    id (as ``list-presets`` reports it, e.g. ``"model-b-disc"``) and looked up
    in the directories the ``variant`` server searches.

    Args:
        preset: A preset file path, or a preset id.
        server: The server installation, binary or install root the machine
            will run on; None applies the default resolution.
        variant: The machine variant, which selects the binary within the
            installation.

    Raises:
        ServerNotFoundError: If no server can be found to resolve against.
        FileNotFoundError: If the id names no preset in any searched directory.
    """
    path = Path(preset)
    if path.is_file():
        return path

    installation = ServerInstallation.default() if server is None else ServerInstallation.coerce(server)
    executable_filepath = installation.executable_filepath(variant)
    preset_id = str(preset)
    dirpaths = _preset_search_dirpaths(executable_filepath, installation)
    for dirpath in dirpaths:
        candidate = dirpath / f"{preset_id}{PRESET_EXTENSION}"
        if candidate.is_file():
            return candidate
    searched = ", ".join(str(d) for d in dirpaths) or "(no preset directories found)"
    raise FileNotFoundError(f"no preset {preset_id!r} for {executable_filepath.name}; searched: {searched}")


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


@pytest.fixture(scope="session")
def beebium_preset(request: pytest.FixtureRequest) -> str | Path | None:
    """The preset ``bbc`` and ``bbc_shared`` launch with: an id or a file path.

    Defaults to ``--beebium-preset``, then the ``beebium_preset`` ini value,
    then None (no preset). Override it in a conftest.py or test module to
    configure every ``bbc`` in scope; keep ``scope="session"`` if ``bbc_shared``
    uses it.
    """
    cli_preset = request.config.getoption("--beebium-preset")
    if cli_preset:
        return cli_preset
    ini_preset = request.config.getini("beebium_preset")
    return ini_preset or None


@pytest.fixture(scope="session")
def beebium_extra_args() -> list[str]:
    """Extra server arguments ``bbc`` and ``bbc_shared`` launch with.

    Empty by default. Override it in a conftest.py or test module; keep
    ``scope="session"`` if ``bbc_shared`` uses it.
    """
    return []


def _launcher(
    stack: contextlib.ExitStack,
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
) -> LaunchBbc:
    """A launch function whose machines ``stack`` tears down."""

    def launch(
        *,
        preset: str | Path | None = None,
        variant: str = DEFAULT_VARIANT,
        extra_args: Sequence[str] = (),
        startup_timeout: float = 10.0,
    ) -> Beebium:
        # The MOS and BASIC fixtures are the Model B's ROMs; any other variant
        # boots the defaults its own server resolves.
        is_model_b = variant == DEFAULT_VARIANT
        try:
            preset_filepath = (
                resolve_preset(preset, server=beebium_server_filepath, variant=variant) if preset is not None else None
            )
            return stack.enter_context(
                Beebium.launch(
                    mos_filepath=mos_filepath if is_model_b else None,
                    basic_filepath=basic_filepath if is_model_b else None,
                    server=beebium_server_filepath,
                    variant=variant,
                    extra_args=list(extra_args),
                    startup_timeout=startup_timeout,
                    preset=preset_filepath,
                )
            )
        except ServerNotFoundError as e:
            pytest.skip(str(e))

    return launch


@pytest.fixture(scope="function")
def launch_bbc(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
) -> Iterator[LaunchBbc]:
    """A factory that launches BBC Micros, each torn down when the test ends.

    ``launch_bbc(preset=None, variant="model-b", extra_args=(),
    startup_timeout=10.0)`` launches a machine and returns it connected. A test
    can launch several (a Model B and a B+, say, or the same program booted
    from disc and loaded into memory), or launch one only after its own setup.
    Skips the test if the server executable is not available.

    Usage:
        def test_both_machines(launch_bbc):
            model_b = launch_bbc(preset="model-b-disc")
            b_plus = launch_bbc(variant="model-b-plus")
    """
    with contextlib.ExitStack() as stack:
        yield _launcher(stack, mos_filepath, basic_filepath, beebium_server_filepath)


@pytest.fixture(scope="function")
def bbc(
    launch_bbc: LaunchBbc,
    beebium_preset: str | Path | None,
    beebium_extra_args: list[str],
) -> Beebium:
    """A fresh BBC Micro instance for each test.

    Launched with the ``beebium_preset`` and ``beebium_extra_args`` defaults;
    with neither set it is a bare Model B with no disc controller.
    Skips the test if the server executable is not available.

    Usage:
        def test_print(bbc):
            bbc.debugger.stop()
            bbc.keyboard.type("PRINT 42")
            bbc.keyboard.press_return()
    """
    return launch_bbc(preset=beebium_preset, extra_args=beebium_extra_args)


@pytest.fixture(scope="module")
def bbc_shared(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
    beebium_preset: str | Path | None,
    beebium_extra_args: list[str],
) -> Iterator[Beebium]:
    """A BBC Micro instance shared across tests in a module.

    Use when tests need to build on each other's state.
    The emulator runs continuously for the module. Configured like ``bbc``.
    Skips the test if the server executable is not available.

    Usage:
        def test_load_program(bbc_shared):
            bbc_shared.memory.load(0x1900, "mygame.bin")

        def test_run_program(bbc_shared):
            # Assumes previous test loaded the program
            bbc_shared.debugger.run_to(0x1900)
    """
    with contextlib.ExitStack() as stack:
        launch = _launcher(stack, mos_filepath, basic_filepath, beebium_server_filepath)
        yield launch(preset=beebium_preset, extra_args=beebium_extra_args)


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
def bbc_tube(launch_bbc: LaunchBbc) -> Beebium:
    """A BBC Micro instance with a 65C02 second processor on the Tube.

    Launches the ``model-b-disc-65c02-copro`` system preset: the 65C02
    coprocessor with a disc filing system, whose Tube host code is what drives
    the coprocessor from the host (a bare Model B has none). It uses a longer
    startup timeout to allow the coprocessor to come up.

    Usage:
        def test_tube_feature(bbc_tube):
            bbc_tube.expect("TUBE")
    """
    return launch_bbc(preset="model-b-disc-65c02-copro", startup_timeout=20.0)
