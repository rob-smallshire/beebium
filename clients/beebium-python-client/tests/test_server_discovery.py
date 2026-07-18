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

"""Server discovery must prefer the checkout's own build over an installed one.

A developer with a released Beebium installed system-wide would otherwise run
their working tree's tests against the installed server. The pairing then fails
at connect with a protocol fingerprint mismatch that says nothing about the
wrong binary having been chosen, which is expensive to diagnose.

The in-repo search previously counted a fixed number of parent directories and
stopped one level short of the repo root, so it never matched and PATH always
won. These tests pin the order and the fact that the search actually finds
something.
"""

from __future__ import annotations

import os
from pathlib import Path

import pytest

from beebium.client.exceptions import ServerNotFoundError
from beebium.client.server import ServerProcess


@pytest.fixture
def finder() -> ServerProcess:
    """A ServerProcess with no __init__ run, for exercising path resolution."""
    return ServerProcess.__new__(ServerProcess)


def _make_executable(dirpath: Path, name: str) -> Path:
    dirpath.mkdir(parents=True, exist_ok=True)
    executable = dirpath / name
    executable.write_text("#!/bin/sh\nexit 0\n")
    executable.chmod(0o755)
    return executable


class TestInRepoBuildDiscovery:
    """The build directory in the surrounding checkout is found and preferred."""

    def test_finds_the_checkout_build(self, finder: ServerProcess) -> None:
        """The search reaches the repo root, not merely the clients directory."""
        found = finder._find_in_repo_build(finder._exe_name("beebium-model-b"))
        if found is None:
            pytest.skip("no build directory in this checkout")
        assert found.is_file()
        # The whole point: a path inside this checkout, not an installed one.
        assert "beebium" in found.parts

    def test_resolved_server_is_the_checkout_build(self, finder: ServerProcess) -> None:
        """With no override, discovery lands on the checkout's own server."""
        in_repo = finder._find_in_repo_build(finder._exe_name("beebium-model-b"))
        if in_repo is None:
            pytest.skip("no build directory in this checkout")
        assert finder._find_server(None) == in_repo


class TestDiscoveryPrecedence:
    """Explicit choices outrank discovery; discovery outranks PATH."""

    def test_explicit_path_wins(self, finder: ServerProcess, tmp_path: Path) -> None:
        explicit = _make_executable(tmp_path, "beebium-model-b")
        assert finder._find_server(explicit) == explicit

    def test_explicit_missing_path_is_an_error(
        self, finder: ServerProcess, tmp_path: Path
    ) -> None:
        with pytest.raises(ServerNotFoundError):
            finder._find_server(tmp_path / "nonexistent")

    def test_environment_variable_wins_over_in_repo_build(
        self, finder: ServerProcess, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
    ) -> None:
        """BEEBIUM_SERVER remains the way to test against a different build."""
        override = _make_executable(tmp_path, "beebium-model-b")
        monkeypatch.setenv("BEEBIUM_SERVER", str(override))
        assert finder._find_server(None) == override

    def test_invalid_environment_variable_is_an_error(
        self, finder: ServerProcess, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
    ) -> None:
        """A wrong BEEBIUM_SERVER fails loudly rather than falling through.

        Silently falling back would reintroduce the failure this guards against.
        """
        monkeypatch.setenv("BEEBIUM_SERVER", str(tmp_path / "nonexistent"))
        with pytest.raises(ServerNotFoundError):
            finder._find_server(None)

    def test_in_repo_build_wins_over_path(
        self, finder: ServerProcess, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
    ) -> None:
        """An installed server on PATH must not shadow the checkout's build."""
        in_repo = finder._find_in_repo_build(finder._exe_name("beebium-model-b"))
        if in_repo is None:
            pytest.skip("no build directory in this checkout")

        installed_dirpath = tmp_path / "installed"
        _make_executable(installed_dirpath, finder._exe_name("beebium-model-b"))
        monkeypatch.setenv(
            "PATH", f"{installed_dirpath}{os.pathsep}{os.environ.get('PATH', '')}"
        )
        monkeypatch.delenv("BEEBIUM_SERVER", raising=False)

        assert finder._find_server(None) == in_repo
