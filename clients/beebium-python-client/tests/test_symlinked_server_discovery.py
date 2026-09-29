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

"""Resource discovery when the server is launched through a symlink (#114).

The server locates its ROMs, presets and bundled discs relative to its own
on-disk location. Homebrew puts a symlink to the real binary on PATH, and users
do the same by hand, so the location used must be the symlink's target, not the
symlink itself. These tests run the checkout's freshly built beebium-model-b
through a symlink in a temporary directory, where there is no share/ tree to
find, and expect the same discovery results as a direct invocation.

They drive the binary with subprocess directly rather than the Beebium client,
because the client would resolve and launch the real path itself.
"""

from __future__ import annotations

import os
import subprocess
import time
from pathlib import Path

import pytest

from beebium.client.pytest_plugin import _find_checkout_server


@pytest.fixture(scope="module")
def checkout_server_filepath(request: pytest.FixtureRequest) -> Path:
    """The checkout's own built beebium-model-b (not a wheel or PATH server).

    Only the checkout build sits beside a roms/ tree that is found by walking
    up from the binary, which is the lookup a symlink breaks.
    """
    server = _find_checkout_server(Path(request.config.rootpath))
    if server is None:
        pytest.skip("checkout build of beebium-model-b not found")
    return server


@pytest.fixture
def symlinked_server_filepath(tmp_path: Path, checkout_server_filepath: Path) -> Path:
    """A bin/beebium-model-b symlink to the real binary, in an otherwise empty tree."""
    bin_dirpath = tmp_path / "bin"
    bin_dirpath.mkdir()
    link_filepath = bin_dirpath / checkout_server_filepath.name
    try:
        os.symlink(checkout_server_filepath, link_filepath)
    except OSError as exc:
        pytest.skip(f"cannot create symlinks here: {exc}")
    return link_filepath


def _clean_env() -> dict[str, str]:
    """The test environment minus any discovery overrides, so the binary must
    find its resources from its own location."""
    env = dict(os.environ)
    for name in ("BEEBIUM_ROM_DIR", "BEEBIUM_DISC_DIR", "BEEBIUM_SERVERS_DIRPATH"):
        env.pop(name, None)
    return env


def test_list_presets_through_symlink_finds_built_in_presets(symlinked_server_filepath: Path) -> None:
    result = subprocess.run(
        [str(symlinked_server_filepath), "list-presets"],
        capture_output=True,
        text=True,
        timeout=30,
        env=_clean_env(),
        check=False,
    )
    assert result.returncode == 0, result.stderr
    assert "model-b-disc" in result.stdout, f"built-in presets not found through symlink:\n{result.stdout}"


def test_start_through_symlink_loads_mos_rom(symlinked_server_filepath: Path) -> None:
    """A bare start must find the MOS ROM from the symlink's target, not
    report 'Cannot find ROM directory'."""
    process = subprocess.Popen(
        [str(symlinked_server_filepath), "--port", "0"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        env=_clean_env(),
    )
    output: list[str] = []
    deadline = time.monotonic() + 10.0
    try:
        assert process.stdout is not None
        # Read line by line until the ROM is reported, the deadline passes,
        # or the server exits (stdout hits EOF) with an error.
        while time.monotonic() < deadline:
            line = process.stdout.readline()
            if not line:
                break
            output.append(line)
            if "Loading MOS ROM:" in line:
                break
    finally:
        # Terminate by our own PID only; never by process name, since other
        # servers may share this machine.
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=10)
        if process.stdout is not None:
            process.stdout.close()

    text = "".join(output)
    assert "Loading MOS ROM:" in text, f"server did not load the MOS ROM through the symlink:\n{text}"
    assert "Cannot find ROM directory" not in text
