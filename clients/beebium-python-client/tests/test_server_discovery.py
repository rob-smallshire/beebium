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


class TestSearchRootDiscovery:
    """A caller can supply the roots to search, not only this module's __file__.

    This is what lets a client installed from a wheel -- whose __file__ is in
    site-packages, with no checkout above it -- still find the checkout's own
    build, by searching from pytest's rootdir instead.
    """

    def test_build_found_under_an_explicit_root(
        self, finder: ServerProcess, tmp_path: Path
    ) -> None:
        """A build directory below a supplied root is found, independent of
        where this module happens to live."""
        checkout = tmp_path / "checkout"
        build_server = _make_executable(
            checkout / "build" / "src" / "server",
            finder._exe_name("beebium-model-b"),
        )
        found = finder._find_in_repo_build(
            finder._exe_name("beebium-model-b"), [checkout]
        )
        assert found == build_server

    def test_build_found_above_an_explicit_root(
        self, finder: ServerProcess, tmp_path: Path
    ) -> None:
        """The supplied root's ancestors are searched too, so rootdir need not
        be the repo root -- it is typically the client subdirectory."""
        checkout = tmp_path / "checkout"
        build_server = _make_executable(
            checkout / "build" / "src" / "server",
            finder._exe_name("beebium-model-b"),
        )
        rootdir = checkout / "clients" / "beebium-python-client"
        rootdir.mkdir(parents=True)
        found = finder._find_in_repo_build(
            finder._exe_name("beebium-model-b"), [rootdir]
        )
        assert found == build_server

    def test_explicit_root_build_beats_path(
        self, finder: ServerProcess, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
    ) -> None:
        """The checkout build resolved from a supplied root outranks a server on
        PATH -- the wheel-install case the pytest plugin depends on."""
        checkout = tmp_path / "checkout"
        build_server = _make_executable(
            checkout / "build" / "src" / "server",
            finder._exe_name("beebium-model-b"),
        )
        installed_dirpath = tmp_path / "installed"
        _make_executable(installed_dirpath, finder._exe_name("beebium-model-b"))
        monkeypatch.setenv(
            "PATH", f"{installed_dirpath}{os.pathsep}{os.environ.get('PATH', '')}"
        )
        monkeypatch.delenv("BEEBIUM_SERVER", raising=False)

        # The plugin resolves the checkout build from rootdir, then passes it as
        # the explicit server path -- which the precedence rules put above PATH.
        resolved = finder._find_in_repo_build(
            finder._exe_name("beebium-model-b"), [checkout]
        )
        assert finder._find_server(resolved) == build_server

    def test_no_build_under_root_returns_none(
        self, finder: ServerProcess, tmp_path: Path
    ) -> None:
        """An empty tree (the installed-wheel-with-no-checkout case) yields
        None so discovery can fall through to PATH."""
        assert (
            finder._find_in_repo_build(
                finder._exe_name("beebium-model-b"), [tmp_path]
            )
            is None
        )


class TestDiscoveryPrecedence:
    """Explicit choices outrank discovery; discovery outranks PATH."""

    def test_explicit_path_wins(self, finder: ServerProcess, tmp_path: Path) -> None:
        explicit = _make_executable(tmp_path, finder._exe_name("beebium-model-b"))
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
        override = _make_executable(tmp_path, finder._exe_name("beebium-model-b"))
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
