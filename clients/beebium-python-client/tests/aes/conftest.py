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

"""What every test here shares: the paths the AES tests read, from Beebium's own test options, and
a plain skip when the server or the host's ROMs are missing. Beebium's own conftest.py gives every
test a private user folder.

The paths go to aes_host, never into os.environ: these tests run in the same pytest process as the
rest of the suite, and a variable set here would reach every server launched after them."""
import os
from pathlib import Path

import pytest

from beebium.client.installation import ServerInstallation

_PLUGIN_DIRPATH = Path(__file__).resolve().parents[4] / "src" / "extensions" / "acorn-aes-coprocessor"


def _server_filepath(config):
    """--beebium-server, as Beebium's tests take it, or the server they would launch."""
    server = config.getoption("--beebium-server")
    if server:
        # absolute(), not resolve(): a model link names the model the multi-call server runs.
        return Path(server).absolute()
    try:
        return ServerInstallation.default().executable_filepath("model-b")
    except Exception:
        return None


def _rom_dirpath(config):
    """--beebium-rom-dir, then BEEBIUM_ROM_DIR, then the checkout's roms/."""
    rom_dirpath = config.getoption("--beebium-rom-dir") or os.environ.get("BEEBIUM_ROM_DIR")
    return Path(rom_dirpath).absolute() if rom_dirpath else _PLUGIN_DIRPATH.parents[2] / "roms"


def pytest_configure(config):
    """Runs before the tests import from aes_host, so they see these paths."""
    import aes_host

    aes_host.configure(_server_filepath(config), _rom_dirpath(config))


@pytest.fixture(autouse=True)
def needs_the_server(request):
    import aes_host

    if not getattr(request.module, "NEEDS_SERVER", True):
        return
    if not Path(aes_host.SERVER).is_file():
        aes_host.missing(f"no server at {aes_host.SERVER!r}: build first, or give --beebium-server")
    if not aes_host.DFS.is_file():
        aes_host.missing(f"no filing system ROM at {aes_host.DFS}: BEEBIUM_ROM_DIR")
