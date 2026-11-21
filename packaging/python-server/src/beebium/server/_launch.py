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

`pip install beebium-server` puts `beebium-model-b` (and the other seven
variants) on PATH. Each runs the bundled multi-call ``beebium-server`` with the
variant as its first argument, then the script's own arguments. On POSIX that is
a thin ``os.execv``, so signals, exit code and stdout behave exactly as the bare
binary. Windows has
no exec that preserves console signal semantics -- ``os.execv`` there spawns a
fresh process and mishandles Ctrl-C and the exit code for the shim -- so on
Windows the server is run as a child and its exit code is propagated. Either
way a client that needs precise signal delivery should launch the binary from
beebium.server.executable_filepath() directly, with the variant as its first
argument, rather than through the shim.
"""

from __future__ import annotations

import os
import sys

from beebium.server._paths import executable_filepath


def _exec(variant: str) -> None:
    filepath = executable_filepath(variant)
    argv = [str(filepath), variant, *sys.argv[1:]]
    if sys.platform == "win32":
        import subprocess

        completed = subprocess.run(argv)
        sys.exit(completed.returncode)
    os.execv(str(filepath), argv)


def model_b() -> None:
    _exec("model-b")


def model_b_plus() -> None:
    _exec("model-b-plus")


def model_b_plus_128k() -> None:
    _exec("model-b-plus-128k")


def model_b_romram() -> None:
    _exec("model-b-romram")


def model_b_atpl_sidewise() -> None:
    _exec("model-b-atpl-sidewise")


def model_b_watford_rom_ram() -> None:
    _exec("model-b-watford-rom-ram")


def model_b_integra_b() -> None:
    _exec("model-b-integra-b")


def master_128() -> None:
    _exec("master-128")
