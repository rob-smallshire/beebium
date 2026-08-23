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

"""Console-script entry points for the bundled server binaries.

Each is a thin ``os.execv`` of the matching binary, so `pip install
beebium-server` gives `beebium-model-b` (and the three other variants) on PATH,
with the same signal, exit-code and stdout behaviour as the bare binary. On
Windows the pip-generated launcher spawns rather than execs, so a client that
needs signal delivery should use beebium.server.executable_filepath() directly.
"""

from __future__ import annotations

import os
import sys

from beebium.server._paths import executable_filepath


def _exec(variant: str) -> None:
    filepath = executable_filepath(variant)
    os.execv(str(filepath), [str(filepath), *sys.argv[1:]])


def model_b() -> None:
    _exec("model-b")


def model_b_plus() -> None:
    _exec("model-b-plus")


def model_b_plus_128k() -> None:
    _exec("model-b-plus-128k")


def model_b_romram() -> None:
    _exec("model-b-romram")
