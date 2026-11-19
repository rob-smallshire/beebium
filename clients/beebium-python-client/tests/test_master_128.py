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

import json
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


def test_keyboard_links_do_not_apply(executable_filepath: Path) -> None:
    """The Master's keyboard has no start-up links: its boot options are in
    CMOS, so the server refuses the options that set links."""
    result = subprocess.run([str(executable_filepath), "start", "--auto-boot"],
                            capture_output=True, text=True, timeout=60)
    assert result.returncode != 0
    assert "no start-up links" in result.stderr


def test_preset_schema_has_no_startup_links_and_names_the_mos_roms(
        executable_filepath: Path) -> None:
    result = subprocess.run([str(executable_filepath), "describe-preset-schema"],
                            capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr
    schema = json.loads(result.stdout)
    assert "startup" not in [section["type"] for section in schema["sections"]]
    default_roms = {rom["slot"]: rom["image"]
                    for rom in _find(schema, "default_roms")}
    assert default_roms[12] == "bbc-basic_4.rom"
    assert default_roms[9] == "acorn-dfs_2_24.rom"
    assert default_roms[15] == "acorn-terminal_1_20_mos_3_20.rom"


def _find(node, key: str):
    """The first value under `key` anywhere in a JSON document."""
    if isinstance(node, dict):
        if key in node:
            return node[key]
        children = node.values()
    elif isinstance(node, list):
        children = node
    else:
        return None
    for child in children:
        found = _find(child, key)
        if found is not None:
            return found
    return None


# ---------------------------------------------------------------------------
# The 1770 (step 3 of docs/plans/master-128-round-1.md)
# ---------------------------------------------------------------------------


def _command(bbc: Beebium, line: str, then_containing: str, seconds: float = 20.0) -> str:
    bbc.keyboard.type("CLS\r")
    assert bbc.run_until_or_timeout(lambda: _at_prompt(bbc, ">"), 10.0), _screen(bbc)
    bbc.keyboard.type(line + "\r")
    assert bbc.run_until_or_timeout(
        lambda: _at_prompt(bbc, then_containing) and _screen(bbc).strip() != ">", seconds), \
        _screen(bbc)
    return _screen(bbc)


def test_adfs_catalogues_a_floppy(launch_bbc, tmp_path: Path) -> None:
    bbc = launch_bbc(preset=PRESET, variant=VARIANT)
    _booted(bbc)
    disc = bbc.disc.create_disc("Acorn ADFS S 160K", tmp_path / "blank.ads")
    bbc.disc.drive(0).insert(disc)
    _command(bbc, "*MOUNT 0", ">")
    screen = _command(bbc, "*CAT", "Drive:0")
    assert re.search(r"Dir\.\s*\$", screen), screen


def test_dfs_catalogues_saves_and_loads(launch_bbc, tmp_path: Path) -> None:
    bbc = launch_bbc(preset=PRESET, variant=VARIANT)
    _booted(bbc)
    disc = bbc.disc.create_disc("Acorn DFS 80T SS 200K", tmp_path / "blank.ssd")
    bbc.disc.drive(0).insert(disc)
    _command(bbc, "*DISC", ">")
    _command(bbc, '10 PRINT "MASTER DISC"', ">")
    _command(bbc, 'SAVE "PROG"', ">")
    screen = _command(bbc, "*CAT", "PROG")
    assert re.search(r"Drive\s*:?\s*0", screen), screen
    _command(bbc, "NEW", ">")
    _command(bbc, 'LOAD "PROG"', ">")
    screen = _command(bbc, "LIST", "MASTER DISC")
    assert '10 PRINT "MASTER DISC"' in screen, screen


def test_the_fdc_name_placeholder_names_the_1770(launch_bbc) -> None:
    bbc = launch_bbc(preset=PRESET, variant=VARIANT)
    preview = bbc.system.preview_machine_description("Master {fdc-controller}")
    assert preview.description == "Master WD1770"
    assert preview.report.inapplicable_keys == ()


# ---------------------------------------------------------------------------
# The small moves (step 4)
# ---------------------------------------------------------------------------


def test_station_is_cmos_byte_0(launch_bbc) -> None:
    """--station 42 is the Master's CMOS byte 0, which the guest reads with
    OSBYTE &A1, and the station number Econet reports."""
    bbc = launch_bbc(preset=PRESET, variant=VARIANT, extra_args=["--station", "42"])
    _booted(bbc)
    status = bbc.econet.status
    assert status.station_id == 42
    assert status.station_in_force == 42
    _command(bbc, 'A%=&A1:X%=0:PRINT "S=";(USR(&FFF4) AND &FF0000) DIV &10000', "S=")
    assert re.search(r"S=\s*42", _screen(bbc)), _screen(bbc)
