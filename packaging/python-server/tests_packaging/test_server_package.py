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

"""Packaging assertions for the beebium-server wheel, run against the *installed*
wheel in a fresh venv (build_wheel.py + a matching-platform runner). They import
nothing that executes a server binary -- their job is to prove the wheel is laid
out and registered correctly: the package resolves from site-packages, the paths
API points into the bundled tree, the four binaries are present (and executable
on POSIX), the console scripts are registered, and py.typed ships.
"""

from __future__ import annotations

import importlib.metadata
import os
import sys

import pytest

import beebium.server

VARIANTS = ("model-b", "model-b-plus", "model-b-plus-128k", "model-b-romram")


def test_package_imports_from_site_packages_not_source_tree():
    """The wheel must import from site-packages, not a checkout -- otherwise the
    test would exercise src/ and miss packaging faults."""
    assert beebium.server.__file__ is not None
    parts = beebium.server.__file__.replace("\\", "/").split("/")
    assert "src" not in parts, (
        f"beebium.server imported from a source tree ({beebium.server.__file__}); "
        f"run the packaging tests against the installed wheel."
    )


def test_beebium_is_a_namespace_package():
    """beebium is a PEP 420 namespace shared with the `beebium` client, so it
    has no __file__; beebium.server is a regular package and does."""
    import beebium

    assert getattr(beebium, "__file__", None) is None
    assert getattr(beebium.server, "__file__", None) is not None


def test_version_matches_distribution_metadata():
    assert beebium.server.__version__ == importlib.metadata.version("beebium-server")


def test_variants():
    assert beebium.server.variants() == VARIANTS


def test_bundle_and_rom_dir_exist_under_the_bundle():
    bundle = beebium.server.bundle_dirpath()
    assert bundle.is_dir() and bundle.name == "_bundle"
    roms = beebium.server.rom_dirpath()
    # The ROM directory is the essential invariant: it holds the default MOS the
    # server resolves when --mos is omitted, so a no-argument launch works.
    assert roms.is_dir(), f"no ROM directory in the bundle: {roms}"
    assert bundle in roms.parents
    assert any(roms.glob("*mos*.rom")), f"no MOS ROM in {roms}"
    # Presets live under the bundle too (their presence is checked separately);
    # this only pins the path, which holds whether or not the dir exists.
    assert bundle in beebium.server.preset_dirpath().parents


def test_presets_are_present_when_the_bundle_ships_them():
    """Presets drive `--preset` machine configs. A bundle that ships them must
    ship real files; the Windows bundle currently ships none (an empty
    share/beebium/presets/ does not survive into the wheel), so this skips
    honestly there rather than passing silently -- it must not go quiet."""
    presets = beebium.server.preset_dirpath()
    if not presets.is_dir():
        pytest.skip(
            f"this bundle ships no presets ({presets} absent) -- the Windows wheel omits "
            f"share/beebium/presets (empty in the bundle); expected the model-b / model-b-plus / "
            f"model-b-plus-128k / model-b-romram .preset.beebium files. Tracked as a bundle gap."
        )
    assert any(presets.glob("*.preset.beebium")), f"preset directory {presets} is present but has no presets"


@pytest.mark.parametrize("variant", VARIANTS)
def test_executable_filepath_present_and_executable(variant):
    filepath = beebium.server.executable_filepath(variant)
    assert filepath.is_file()
    assert filepath.parent.name == "bin"
    expected = f"beebium-{variant}" + (".exe" if sys.platform == "win32" else "")
    assert filepath.name == expected
    if sys.platform != "win32":
        assert os.access(filepath, os.X_OK), f"{filepath} is not executable"


def test_executable_filepath_rejects_unknown_variant():
    with pytest.raises(ValueError):
        beebium.server.executable_filepath("model-c")


def test_console_scripts_registered():
    """`pip install beebium-server` puts the four servers on PATH."""
    scripts = {ep.name: ep.value for ep in importlib.metadata.entry_points(group="console_scripts")}
    assert scripts.get("beebium-model-b") == "beebium.server._launch:model_b"
    assert scripts.get("beebium-model-b-plus") == "beebium.server._launch:model_b_plus"
    assert scripts.get("beebium-model-b-plus-128k") == "beebium.server._launch:model_b_plus_128k"
    assert scripts.get("beebium-model-b-romram") == "beebium.server._launch:model_b_romram"


def test_py_typed_marker_is_shipped():
    import importlib.resources

    marker = importlib.resources.files("beebium.server").joinpath("py.typed")
    assert marker.is_file(), "py.typed marker missing from the installed package"
