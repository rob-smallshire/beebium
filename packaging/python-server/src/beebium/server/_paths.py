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

"""Locate the server binaries and data shipped inside this wheel.

The bundle under ``_bundle/`` is the release tarball laid out verbatim
(``bin/ lib/ share/``), so the server's own relative discovery of ROMs, presets,
extensions and ABI libraries works unchanged -- the binaries do not know they
are inside a Python package.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

from beebium.server._version import __version__

__all__ = [
    "__version__",
    "bundle_dirpath",
    "executable_filepath",
    "preset_dirpath",
    "rom_dirpath",
    "variants",
]

# The machine variants, as the suffix of the binary name (beebium-<variant>).
# "model-b" is the default everywhere, matching the client.
_VARIANTS = ("model-b", "model-b-plus", "model-b-plus-128k", "model-b-romram")


def variants() -> tuple[str, ...]:
    """The machine variants shipped in this wheel."""
    return _VARIANTS


def bundle_dirpath() -> Path:
    """Path to the bundled install tree (its ``bin/ lib/ share/`` root)."""
    return Path(__file__).resolve().parent / "_bundle"


def _executable_name(variant: str) -> str:
    name = f"beebium-{variant}"
    if sys.platform == "win32":
        name += ".exe"
    return name


def executable_filepath(variant: str = "model-b") -> Path:
    """Path to a bundled server executable, ensured executable.

    Zip entries carry Unix mode bits and pip preserves them, so the binary
    normally arrives executable. As belt and braces this repairs a missing
    execute bit (a one-time, idempotent chmod) for installers that drop it.

    Raises:
        ValueError: if ``variant`` is not one of ``variants()``.
        FileNotFoundError: if the binary is missing from the bundle.
    """
    if variant not in _VARIANTS:
        raise ValueError(f"unknown variant {variant!r}; expected one of {', '.join(_VARIANTS)}")
    filepath = bundle_dirpath() / "bin" / _executable_name(variant)
    if not filepath.exists():
        raise FileNotFoundError(f"server binary not found in the bundle: {filepath}")
    if sys.platform != "win32" and not os.access(filepath, os.X_OK):
        mode = filepath.stat().st_mode
        filepath.chmod(mode | 0o111)  # add execute for user/group/other
    return filepath


def rom_dirpath() -> Path:
    """Path to the bundled ROM directory (``share/beebium/roms``)."""
    return bundle_dirpath() / "share" / "beebium" / "roms"


def preset_dirpath() -> Path:
    """Path to the bundled preset directory (``share/beebium/presets``)."""
    return bundle_dirpath() / "share" / "beebium" / "presets"
