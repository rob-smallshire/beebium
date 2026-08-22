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

"""The pytest plugin must locate ROMs and the server from pytest's rootdir.

When beebium is installed from a wheel its own __file__ sits in site-packages
with no checkout above it, so the ROM directory and the freshly-built server
can no longer be found by walking up from the module. pytest's rootdir still
points into the checkout under test (tests/ live there), so the plugin anchors
on it. These tests exercise the pure helpers that do that, driven by a
synthetic rootdir so they need no emulator server.
"""

from __future__ import annotations

from pathlib import Path

from beebium.client.pytest_plugin import _find_checkout_roms, _find_checkout_server
from beebium.client.server import ServerProcess


def _make_executable(dirpath: Path, name: str) -> Path:
    dirpath.mkdir(parents=True, exist_ok=True)
    executable = dirpath / name
    executable.write_text("#!/bin/sh\nexit 0\n")
    executable.chmod(0o755)
    return executable


class TestCheckoutRoms:
    """roms/ is found at or above rootdir, wheel install or not."""

    def test_roms_found_at_rootdir(self, tmp_path: Path) -> None:
        (tmp_path / "roms").mkdir()
        assert _find_checkout_roms(tmp_path) == tmp_path / "roms"

    def test_roms_found_above_rootdir(self, tmp_path: Path) -> None:
        """rootdir is typically the client subdirectory; roms/ lives at the
        repo root above it."""
        (tmp_path / "roms").mkdir()
        rootdir = tmp_path / "clients" / "beebium-python-client"
        rootdir.mkdir(parents=True)
        assert _find_checkout_roms(rootdir) == tmp_path / "roms"

    def test_no_roms_returns_none(self, tmp_path: Path) -> None:
        assert _find_checkout_roms(tmp_path) is None


class TestCheckoutServer:
    """The server build is found at or above rootdir."""

    def test_server_found_above_rootdir(self, tmp_path: Path) -> None:
        exe_name = ServerProcess._exe_name("beebium-model-b")
        build_server = _make_executable(
            tmp_path / "build" / "src" / "server", exe_name
        )
        rootdir = tmp_path / "clients" / "beebium-python-client"
        rootdir.mkdir(parents=True)
        assert _find_checkout_server(rootdir) == build_server

    def test_no_server_returns_none(self, tmp_path: Path) -> None:
        assert _find_checkout_server(tmp_path) is None
