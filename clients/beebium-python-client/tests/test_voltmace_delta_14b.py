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

"""The Voltmace Delta 14B/1 adapter driven through ControllerService (#98).

Test 16 of docs/discussion/voltmace-delta-14b-specification.md. The BASIC
programs are tokenised with oaknut-basic and put on a disc image with
oaknut-disc, then CHAINed; nothing is typed in but the CHAIN.
"""

from __future__ import annotations

import re
import shutil
import subprocess
from collections.abc import Iterator
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError

OAKNUT_DISC = ["uvx", "--from", "oaknut-disc>=12.10.0", "oaknut-disc"]
OAKNUT_BASIC = ["uvx", "oaknut-basic"]

# The manual's six-strobe scan, side socket then rear: the first key found
# and its socket, or NONE -1.
SCAN_PROGRAM = """\
10 *FX151,98,240
20 DATA &E0,&D0,&B0,&60,&50,&30
30 H$="NONE":K%=-1
40 FOR I%=0 TO 5
50 READ S%
60 A%=&97:X%=&60:Y%=S%:CALL &FFF4
70 A%=&96:X%=&60:R%=((USR(&FFF4) AND &FF0000) DIV &10000) AND 15
80 C%=I% MOD 3:W%=-1
90 IF R%=7 THEN W%=0
100 IF R%=11 THEN W%=1
110 IF R%=13 THEN W%=2
120 IF R%=14 THEN W%=3
130 IF W%>=0 AND K%<0 THEN K%=3*W%+C%:H$=MID$("SIDEREAR",1+4*(I% DIV 3),4)
140 NEXT
150 PRINT "VSC16 ";H$;" ";K%;" ENDS16"
"""

# Handset 1's X, the rear socket's stick, on ADVAL(1).
ADVAL_PROGRAM = """\
10 T%=TIME:REPEAT UNTIL TIME>T%+30
20 PRINT "VAD16 ";~ADVAL(1);" ENDA16"
"""


def _run(args: list[str]) -> None:
    subprocess.run(args, check=True, capture_output=True)


@pytest.fixture(scope="module")
def voltmace_disc_filepath(tmp_path_factory: pytest.TempPathFactory) -> Path:
    if shutil.which("uvx") is None:
        pytest.skip("uvx (from uv) is needed to build the BASIC disc")
    dirpath = tmp_path_factory.mktemp("voltmace-disc")
    disc_filepath = dirpath / "voltmace.ssd"
    _run([*OAKNUT_DISC, "create", str(disc_filepath), "--title", "VOLTMACE"])
    for name, text in (("VSC", SCAN_PROGRAM), ("VAD", ADVAL_PROGRAM)):
        source_filepath = dirpath / f"{name}.bas"
        source_filepath.write_text(text, encoding="ascii")
        tokenised_filepath = dirpath / f"{name}.tok"
        _run([*OAKNUT_BASIC, "tokenise", str(source_filepath), str(tokenised_filepath)])
        _run([*OAKNUT_DISC, "put", "--load", "0x1900", "--exec", "0x8023",
              f"{disc_filepath}:$.{name}", str(tokenised_filepath)])
    return disc_filepath


@pytest.fixture(scope="module")
def bbc_voltmace(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
    dfs_1770_rom_filepath: Path,
    voltmace_disc_filepath: Path,
) -> Iterator[Beebium]:
    """A Model B with the adapter, a 1770, DFS 2.26 and the programs in drive 0."""
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server=beebium_server_filepath,
            extra_args=[
                "--voltmace-delta-14b",
                "--fdc",
                "acorn-1770",
                "--sideways",
                f"slot=14:type=rom:image={dfs_1770_rom_filepath}",
            ],
            startup_timeout=20.0,
        ) as bbc:
            bbc.disc.drive(0).insert(voltmace_disc_filepath, write_protect=True)
            bbc.basic.wait_for_prompt(timeout=20.0)
            yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def _chain_and_read(bbc: Beebium, program: str, tag: str, end: str) -> list[str]:
    """CHAIN a program and return the fields of its result line, which
    starts with `tag` and ends with `end`."""
    bbc.keyboard.type(f'CHAIN "{program}"\r')
    bbc.basic.wait_for_text(f" {end}", timeout=30.0)
    screen = bbc.video.screen_text().text
    match = re.search(rf"{tag} (.*?) {end}", screen)
    assert match, screen
    return match.group(1).split()


def test_a_side_key_set_through_the_service_is_found_by_the_six_strobe_scan(
    bbc_voltmace: Beebium,
) -> None:
    bbc_voltmace.controllers.set("voltmace-delta-14b", {"handset2.key4": True})
    try:
        assert _chain_and_read(bbc_voltmace, "VSC", "VSC16", "ENDS16") == ["SIDE", "4"]
    finally:
        bbc_voltmace.controllers.set("voltmace-delta-14b", {"handset2.key4": False})


def test_handset_1_x_is_adval_1(bbc_voltmace: Beebium) -> None:
    bbc_voltmace.controllers.set("voltmace-delta-14b", {"handset1.x": 0xF000})
    (value,) = _chain_and_read(bbc_voltmace, "VAD", "VAD16", "ENDA16")
    assert 0xF000 <= int(value, 16) <= 0xF0FF
