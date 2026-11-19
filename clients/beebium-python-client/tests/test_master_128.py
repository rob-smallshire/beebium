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

"""The BBC Master 128 server (#189): the master-128 preset boots MOS 3.20 to
BASIC in Mode 7 on ADFS, and a saved machine keeps its *CONFIGURE settings in
the clock's battery-backed CMOS from one run to the next.

The paging rules, the clock on the System VIA and the CMOS seed are tested in
C++ (tests/test_master_128_*.cpp).
"""

from __future__ import annotations

import re
import subprocess
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.installation import ServerInstallation

VARIANT = "master-128"
PRESET = "master-128"
# OSBYTE &87: the screen mode in Y.
PRINT_MODE = 'A%=&87:PRINT "MODE=";(USR(&FFF4) AND &FF0000) DIV &10000'


def _screen(bbc: Beebium) -> str:
    return bbc.video.screen_text().text


def _at_prompt(bbc: Beebium, containing: str) -> bool:
    lines = [ln.rstrip() for ln in _screen(bbc).splitlines() if ln.strip()]
    return (bbc.keyboard.typing_status().idle and bool(lines) and lines[-1] == ">"
            and containing in _screen(bbc))


def _booted(bbc: Beebium) -> None:
    bbc.system.set_speed_multiplier(0.0)
    assert bbc.run_until_or_timeout(lambda: _at_prompt(bbc, "BASIC"), 10.0), _screen(bbc)


def _type(bbc: Beebium, line: str, then_containing: str) -> None:
    bbc.keyboard.type(line + "\r")
    assert bbc.run_until_or_timeout(lambda: _at_prompt(bbc, then_containing), 10.0), \
        _screen(bbc)


@pytest.fixture
def executable_filepath(beebium_server_filepath: Path | None) -> Path:
    try:
        installation = (ServerInstallation.default() if beebium_server_filepath is None
                        else ServerInstallation.coerce(beebium_server_filepath))
        return installation.executable_filepath(VARIANT)
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def test_boots_mos_3_20_to_basic_in_mode_7_on_adfs(launch_bbc) -> None:
    bbc = launch_bbc(preset=PRESET, variant=VARIANT)
    _booted(bbc)
    screen = _screen(bbc)
    assert "Acorn MOS" in screen
    assert "Acorn ADFS" in screen
    _type(bbc, PRINT_MODE, "MODE=")
    assert re.search(r"MODE=\s*7", _screen(bbc)), _screen(bbc)


def test_configure_survives_save_and_relaunch(executable_filepath: Path,
                                              tmp_path: Path) -> None:
    machine = tmp_path / "Master.beebium-machine"
    created = subprocess.run(
        [str(executable_filepath), "create-machine", "--preset", PRESET, "--output",
         str(machine)],
        capture_output=True, text=True, timeout=60)
    assert created.returncode == 0, created.stderr

    with Beebium.launch(server=executable_filepath,
                        extra_args=["--machine", str(machine)]) as bbc:
        _booted(bbc)
        _type(bbc, "*CONFIGURE MODE 0", ">")
    assert (machine / "nvram" / "master-128-cmos.bin").is_file()

    with Beebium.launch(server=executable_filepath,
                        extra_args=["--machine", str(machine)]) as bbc:
        _booted(bbc)
        _type(bbc, PRINT_MODE, "MODE=")
        assert re.search(r"MODE=\s*0", _screen(bbc)), _screen(bbc)
