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

"""Prebuilt Beebium server binaries, shipped as a platform wheel.

    import beebium.server

    beebium.server.__version__
    beebium.server.executable_filepath(variant="model-b")   # ensured executable
    beebium.server.rom_dirpath()
    beebium.server.preset_dirpath()
    beebium.server.variants()
    beebium.server.bundle_dirpath()

This package is dependency-free and usable without the `beebium` client; the
client discovers it (when installed) as one of its server installations.
"""

from beebium.server._paths import (
    __version__,
    bundle_dirpath,
    executable_filepath,
    preset_dirpath,
    rom_dirpath,
    variants,
)

__all__ = [
    "__version__",
    "bundle_dirpath",
    "executable_filepath",
    "preset_dirpath",
    "rom_dirpath",
    "variants",
]
