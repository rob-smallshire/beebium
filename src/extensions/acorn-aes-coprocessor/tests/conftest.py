# Copyright 2026 James Sargent <acheton1984@letterboxes.org>
#
# This file is part of AESulator, a plugin for Beebium.
#
# AESulator is free software: you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version. AESulator is distributed in the hope that it will
# be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# You should have received a copy of the GNU General Public License along with AESulator.
# If not, see <https://www.gnu.org/licenses/>.

# AESulator: the Acorn ARM Evaluation System, a Beebium plugin.
# conftest.py, what every Python test shares
# version 2.0.0, 20 November 2026.

"""What every test here shares: a private user folder, as Beebium's own client tests have (SRC,
its conftest.py), and a plain skip when the server or the host's ROMs are missing."""
import os
from pathlib import Path

import pytest

import aes_host


@pytest.fixture(scope="session", autouse=True)
def private_user_folder(tmp_path_factory):
    """No test reads or writes the owner's real Beebium presets, discs or ROMs."""
    previous = os.environ.get("BEEBIUM_USER_DIRPATH")
    os.environ["BEEBIUM_USER_DIRPATH"] = str(tmp_path_factory.mktemp("user-state"))
    yield
    if previous is None:
        del os.environ["BEEBIUM_USER_DIRPATH"]
    else:
        os.environ["BEEBIUM_USER_DIRPATH"] = previous


@pytest.fixture(autouse=True)
def needs_the_server(request):
    if not getattr(request.module, "NEEDS_SERVER", True):
        return
    if not Path(aes_host.SERVER).is_file():
        aes_host.missing(f"no server at BEEBIUM_SERVER, {aes_host.SERVER!r}: build first")
    if not aes_host.DFS.is_file():
        aes_host.missing(f"no filing system ROM at {aes_host.DFS}: BEEBIUM_ROM_DIR")
