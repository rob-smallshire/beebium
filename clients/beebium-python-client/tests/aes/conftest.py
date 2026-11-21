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
test a private user folder."""
import os
from pathlib import Path

import pytest

from beebium.client.installation import ServerInstallation

_PLUGIN_DIRPATH = Path(__file__).resolve().parents[4] / "src" / "extensions" / "acorn-aes-coprocessor"


def pytest_configure(config):
    """Runs before aes_host is imported, which reads these at import. --beebium-server and
    --beebium-rom-dir, as Beebium's tests take them, outrank the environment."""
    server = config.getoption("--beebium-server")
    if server:
        # absolute(), not resolve(): a model link names the model the multi-call server runs.
        os.environ["BEEBIUM_SERVER"] = str(Path(server).absolute())
    rom_dirpath = config.getoption("--beebium-rom-dir")
    if rom_dirpath:
        os.environ["BEEBIUM_ROM_DIR"] = str(Path(rom_dirpath).absolute())
    os.environ.setdefault("BEEBIUM_ROM_DIR", str(_PLUGIN_DIRPATH.parents[2] / "roms"))
    os.environ.setdefault("AES_PLUGIN_DIR", str(_PLUGIN_DIRPATH))
    try:
        server_filepath = ServerInstallation.default().executable_filepath("model-b")
    except Exception:
        return
    os.environ["AES_SERVER_FILEPATH"] = str(server_filepath)
    # The server is <build>/src/server/beebium-model-b; the plugin's tools are built beside it.
    tools_dirpath = server_filepath.parent.parent / "extensions" / "acorn-aes-coprocessor"
    os.environ.setdefault("AES_BANNER_TOOL", str(tools_dirpath / "aes-banner"))
    os.environ.setdefault("AES_TUBE_LOG_DIR", str(tools_dirpath / "tube-log"))


@pytest.fixture(autouse=True)
def needs_the_server(request):
    import aes_host

    if not getattr(request.module, "NEEDS_SERVER", True):
        return
    if not Path(aes_host.SERVER).is_file():
        aes_host.missing(f"no server at {aes_host.SERVER!r}: build first, or give --beebium-server")
    if not aes_host.DFS.is_file():
        aes_host.missing(f"no filing system ROM at {aes_host.DFS}: BEEBIUM_ROM_DIR")
