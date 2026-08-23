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

"""Choosing a server: the explicit installation forms and the default order.

A wrong pairing must never be chosen silently, and when the user is specific
that is what runs -- no fallback. When the user is not specific, resolution goes
BEEBIUM_SERVER -> checkout build -> the beebium-server wheel -> PATH, with the
wheel outranking PATH because it is version-locked to the client by construction.
"""

from __future__ import annotations

import os
import sys
import types
from pathlib import Path

import pytest

from beebium.client.exceptions import ServerNotFoundError
from beebium.client.installation import (
    VARIANTS,
    ServerInstallation,
    _exe_name,
    find_in_build_tree,
)
from beebium.client.server import ServerProcess


def _make_executable(dirpath: Path, name: str) -> Path:
    dirpath.mkdir(parents=True, exist_ok=True)
    executable = dirpath / name
    executable.write_text("#!/bin/sh\nexit 0\n")
    executable.chmod(0o755)
    return executable


def _make_install_tree(root: Path, *, variants: tuple[str, ...] = VARIANTS, with_share: bool = True) -> Path:
    """A prefix with bin/<all variants> and (optionally) share/beebium/{roms,presets}."""
    for variant in variants:
        _make_executable(root / "bin", _exe_name(variant))
    if with_share:
        (root / "share" / "beebium" / "roms").mkdir(parents=True, exist_ok=True)
        (root / "share" / "beebium" / "presets").mkdir(parents=True, exist_ok=True)
    return root


def _inject_fake_wheel(monkeypatch: pytest.MonkeyPatch, bundle_root: Path, *, version: str = "0.1.3") -> None:
    """Put a fake importable `beebium.server` on sys.modules pointing at a tree,
    so the resolution logic can be exercised without a real platform wheel."""
    _make_install_tree(bundle_root)
    module = types.ModuleType("beebium.server")
    module.__version__ = version
    module.bundle_dirpath = lambda: bundle_root
    module.rom_dirpath = lambda: bundle_root / "share" / "beebium" / "roms"
    module.preset_dirpath = lambda: bundle_root / "share" / "beebium" / "presets"
    module.variants = lambda: VARIANTS
    monkeypatch.setitem(sys.modules, "beebium.server", module)


class TestFindInBuildTree:
    """The checkout build search, reused by the pytest plugin from rootdir."""

    def test_build_found_at_and_above_a_root(self, tmp_path: Path) -> None:
        checkout = tmp_path / "checkout"
        server = _make_executable(checkout / "build" / "src" / "server", _exe_name("model-b"))
        assert find_in_build_tree(_exe_name("model-b"), [checkout]) == server
        rootdir = checkout / "clients" / "beebium-python-client"
        rootdir.mkdir(parents=True)
        assert find_in_build_tree(_exe_name("model-b"), [rootdir]) == server

    def test_no_build_returns_none(self, tmp_path: Path) -> None:
        assert find_in_build_tree(_exe_name("model-b"), [tmp_path]) is None

    def test_serverprocess_delegates(self, tmp_path: Path) -> None:
        server = _make_executable(tmp_path / "build" / "src" / "server", _exe_name("model-b"))
        assert ServerProcess._find_in_repo_build(_exe_name("model-b"), [tmp_path]) == server


class TestExplicitForms:
    """When the user is specific, that installation is what runs."""

    def test_from_root_prefix(self, tmp_path: Path) -> None:
        root = _make_install_tree(tmp_path / "inst")
        installation = ServerInstallation.from_root(root)
        assert installation.executable_filepath().name == _exe_name("model-b")
        assert installation.executable_filepath("model-b-plus").name == _exe_name("model-b-plus")
        assert installation.variants() == VARIANTS
        assert installation.rom_dirpath == root / "share" / "beebium" / "roms"
        assert installation.preset_dirpath == root / "share" / "beebium" / "presets"

    def test_from_root_accepts_a_bin_directory(self, tmp_path: Path) -> None:
        root = _make_install_tree(tmp_path / "inst")
        installation = ServerInstallation.from_root(root / "bin")
        assert installation.root_dirpath == root
        assert installation.rom_dirpath == root / "share" / "beebium" / "roms"

    def test_from_root_without_binaries_is_an_error(self, tmp_path: Path) -> None:
        (tmp_path / "empty").mkdir()
        with pytest.raises(ServerNotFoundError):
            ServerInstallation.from_root(tmp_path / "empty")

    def test_from_executable_returns_the_exact_binary(self, tmp_path: Path) -> None:
        binary = _make_executable(tmp_path / "bare", _exe_name("model-b"))
        installation = ServerInstallation.from_executable(binary)
        assert installation.executable_filepath() == binary
        # A bare binary has no share/ tree, so no ROM/preset dirs.
        assert installation.rom_dirpath is None and installation.preset_dirpath is None
        assert installation.variants() == ("model-b",)

    def test_from_executable_missing_binary_is_an_error(self, tmp_path: Path) -> None:
        with pytest.raises(ServerNotFoundError):
            ServerInstallation.from_executable(tmp_path / "nonexistent")

    def test_missing_variant_error_names_the_installation(self, tmp_path: Path) -> None:
        binary = _make_executable(tmp_path / "bare", _exe_name("model-b"))
        installation = ServerInstallation.from_executable(binary)
        with pytest.raises(ServerNotFoundError) as excinfo:
            installation.executable_filepath("model-b-plus")
        assert "explicit executable" in str(excinfo.value)

    def test_unknown_variant_is_a_value_error(self, tmp_path: Path) -> None:
        installation = ServerInstallation.from_root(_make_install_tree(tmp_path / "inst"))
        with pytest.raises(ValueError):
            installation.executable_filepath("model-c")

    def test_coerce_directory_and_file(self, tmp_path: Path) -> None:
        root = _make_install_tree(tmp_path / "inst")
        assert ServerInstallation.coerce(str(root)).origin == "install root"
        assert ServerInstallation.coerce(root / "bin" / _exe_name("model-b")).origin == "explicit executable"
        # An installation passes through unchanged.
        installation = ServerInstallation.from_root(root)
        assert ServerInstallation.coerce(installation) is installation

    def test_executable_permission_is_repaired(self, tmp_path: Path) -> None:
        if sys.platform == "win32":
            pytest.skip("no execute bit on Windows")
        binary = _make_executable(tmp_path / "bin", _exe_name("model-b"))
        binary.chmod(0o644)  # drop the execute bit
        installation = ServerInstallation.from_root(tmp_path)
        resolved = installation.executable_filepath()
        assert os.access(resolved, os.X_OK)


class TestDefaultResolution:
    """BEEBIUM_SERVER -> checkout build -> wheel -> PATH, when not specific."""

    @pytest.fixture(autouse=True)
    def _no_checkout_build(self, monkeypatch: pytest.MonkeyPatch) -> None:
        # Neutralise the checkout-build step so this machine's own repo build
        # does not short-circuit the wheel/PATH ordering under test.
        monkeypatch.setattr("beebium.client.installation.find_in_build_tree", lambda *a, **k: None)

    def test_env_var_beats_the_wheel(self, tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
        _inject_fake_wheel(monkeypatch, tmp_path / "wheel")
        env_server = _make_executable(tmp_path / "env", _exe_name("model-b"))
        monkeypatch.setenv("BEEBIUM_SERVER", str(env_server))
        installation = ServerInstallation.default()
        assert installation.origin == "BEEBIUM_SERVER"
        assert installation.executable_filepath() == env_server

    def test_wheel_beats_path(self, tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
        monkeypatch.delenv("BEEBIUM_SERVER", raising=False)
        _inject_fake_wheel(monkeypatch, tmp_path / "wheel")
        on_path = tmp_path / "onpath"
        _make_executable(on_path, _exe_name("model-b"))
        monkeypatch.setenv("PATH", f"{on_path}{os.pathsep}{os.environ.get('PATH', '')}")
        installation = ServerInstallation.default()
        assert installation.origin == "beebium-server wheel"

    def test_path_used_when_no_wheel(self, tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
        monkeypatch.delenv("BEEBIUM_SERVER", raising=False)
        monkeypatch.delitem(sys.modules, "beebium.server", raising=False)
        monkeypatch.setattr(
            ServerInstallation, "installed_wheel",
            classmethod(lambda cls: (_ for _ in ()).throw(ImportError())),
        )
        on_path = tmp_path / "onpath"
        _make_executable(on_path, _exe_name("model-b"))
        monkeypatch.setenv("PATH", f"{on_path}{os.pathsep}{os.environ.get('PATH', '')}")
        installation = ServerInstallation.default()
        assert installation.origin == "PATH"

    def test_nothing_found_is_an_error(self, tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
        monkeypatch.delenv("BEEBIUM_SERVER", raising=False)
        monkeypatch.setattr(
            ServerInstallation, "installed_wheel",
            classmethod(lambda cls: (_ for _ in ()).throw(ImportError())),
        )
        monkeypatch.setenv("PATH", str(tmp_path / "empty"))
        with pytest.raises(ServerNotFoundError):
            ServerInstallation.default()

    def test_wheel_version_skew_warns(self, tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
        monkeypatch.delenv("BEEBIUM_SERVER", raising=False)
        _inject_fake_wheel(monkeypatch, tmp_path / "wheel", version="9.9.9")
        with pytest.warns(UserWarning, match="differs from the beebium client"):
            ServerInstallation.default()


class TestServerProcessIntegration:
    """ServerProcess resolves an installation and honours server=/variant=/mos."""

    def test_deprecated_server_filepath_alias_warns(self, tmp_path: Path) -> None:
        binary = _make_executable(tmp_path / "bin", _exe_name("model-b"))
        with pytest.warns(DeprecationWarning, match="server_filepath="):
            process = ServerProcess(server_filepath=binary)
        assert process.server_filepath == binary

    def test_server_and_alias_together_is_an_error(self, tmp_path: Path) -> None:
        binary = _make_executable(tmp_path / "bin", _exe_name("model-b"))
        with pytest.raises(ValueError):
            ServerProcess(server=binary, server_filepath=binary)

    def test_variant_selects_the_binary(self, tmp_path: Path) -> None:
        root = _make_install_tree(tmp_path / "inst")
        process = ServerProcess(server=root, variant="model-b-plus")
        assert process.server_filepath.name == _exe_name("model-b-plus")
        assert process.variant == "model-b-plus"
        assert process.installation.origin == "install root"

    def test_mos_omitted_from_command_when_none(self, tmp_path: Path) -> None:
        binary = _make_executable(tmp_path / "bin", _exe_name("model-b"))
        process = ServerProcess(server=binary)
        assert "--mos" not in process._build_command()

    def test_mos_included_when_given(self, tmp_path: Path) -> None:
        binary = _make_executable(tmp_path / "bin", _exe_name("model-b"))
        mos = tmp_path / "mos.rom"
        mos.write_bytes(b"\x00" * 16)
        process = ServerProcess(server=binary, mos_filepath=mos)
        command = process._build_command()
        assert "--mos" in command and str(mos) in command
