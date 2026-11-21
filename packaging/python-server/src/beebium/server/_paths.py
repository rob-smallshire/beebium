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
extensions and ABI libraries works unchanged -- the binary does not know it is
inside a Python package.

Every variant is one multi-call executable, ``bin/beebium-server``, which takes
the model as its first argument (``beebium-server model-b --port 48875``). The
release bundles also carry a ``beebium-<model>`` link (a launcher on Windows)
per variant; the wheel leaves those out, since a zip cannot hold a symlink and
following one would store the whole binary again for each model.
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

# The machine variants: the model ids beebium-server takes as its first
# argument, and the suffix of the per-model names (beebium-<variant>) outside the
# wheel. "model-b" is the default everywhere, matching the client.
_VARIANTS = ("model-b", "model-b-plus", "model-b-plus-128k", "model-b-romram",
             "model-b-atpl-sidewise", "model-b-watford-rom-ram", "model-b-integra-b", "master-128")


def variants() -> tuple[str, ...]:
    """The machine variants shipped in this wheel."""
    return _VARIANTS


def bundle_dirpath() -> Path:
    """Path to the bundled install tree (its ``bin/ lib/ share/`` root)."""
    return Path(__file__).resolve().parent / "_bundle"


def _executable_name() -> str:
    return "beebium-server.exe" if sys.platform == "win32" else "beebium-server"


def executable_filepath(variant: str = "model-b") -> Path:
    """Path to the bundled server executable that runs ``variant``, ensured executable.

    This is ``bin/beebium-server`` for every variant. Run it with the variant as
    its first argument: ``[executable_filepath(v), v, *args]``.

    Zip entries carry Unix mode bits and pip preserves them, so the binary
    normally arrives executable. As belt and braces this repairs a missing
    execute bit (a one-time, idempotent chmod) for installers that drop it.

    Raises:
        ValueError: if ``variant`` is not one of ``variants()``.
        FileNotFoundError: if the binary is missing from the bundle.
    """
    if variant not in _VARIANTS:
        raise ValueError(f"unknown variant {variant!r}; expected one of {', '.join(_VARIANTS)}")
    filepath = bundle_dirpath() / "bin" / _executable_name()
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
