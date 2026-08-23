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

"""Hatchling build hook that stamps the platform tag on the wheel.

The wheel carries native binaries, so it is not pure Python and must not be
tagged ``py3-none-any``. build_wheel.py sets ``BEEBIUM_WHEEL_TAG`` (e.g.
``manylinux_2_36_aarch64``); this marks the wheel non-pure and sets its tag to
``py3-none-<tag>``, so a single ``uv build --wheel`` emits the correctly named
wheel with no post-build retag. With the variable unset the build stays
pure-Python (``py3-none-any``), which is only useful for local inspection.
"""

from __future__ import annotations

import os
from typing import Any

from hatchling.builders.hooks.plugin.interface import BuildHookInterface


class CustomBuildHook(BuildHookInterface):
    PLUGIN_NAME = "custom"

    def initialize(self, version: str, build_data: dict[str, Any]) -> None:
        tag = os.environ.get("BEEBIUM_WHEEL_TAG")
        if tag:
            build_data["pure_python"] = False
            build_data["tag"] = f"py3-none-{tag}"
