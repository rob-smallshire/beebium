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

"""A joystick driven through ControllerService and read from BBC BASIC (#98).

Tests 14 and 15 of docs/discussion/controller-service-specification.md. The
BASIC program is tokenised with oaknut-basic and put on a disc image with
oaknut-disc, then CHAINed; nothing is typed in but the CHAIN.
"""

from __future__ import annotations

import re
import shutil
import subprocess
from collections.abc import Iterator
from pathlib import Path

import grpc
import pytest

from beebium.client import Beebium
from beebium.client.controller import ControlKind
from beebium.client.exceptions import ServerNotFoundError

OAKNUT_DISC = ["uvx", "--from", "oaknut-disc>=12.10.0", "oaknut-disc"]
OAKNUT_BASIC = ["uvx", "oaknut-basic"]

# ADVAL(1), stick 1's x, and the left-side fire button in ADVAL(0)'s bit 0.
ADVAL_PROGRAM = """\
10 T%=TIME:REPEAT UNTIL TIME>T%+30
20 PRINT "JOY14 ";~ADVAL(1);" ";ADVAL(0) AND 1;" END14"
"""


def _run(args: list[str]) -> None:
    subprocess.run(args, check=True, capture_output=True)


@pytest.fixture(scope="module")
def joystick_disc_filepath(tmp_path_factory: pytest.TempPathFactory) -> Path:
    if shutil.which("uvx") is None:
        pytest.skip("uvx (from uv) is needed to build the BASIC disc")
    dirpath = tmp_path_factory.mktemp("joystick-disc")
    disc_filepath = dirpath / "joystick.ssd"
    _run([*OAKNUT_DISC, "create", str(disc_filepath), "--title", "JOYSTICK"])
    source_filepath = dirpath / "J14.bas"
    source_filepath.write_text(ADVAL_PROGRAM, encoding="ascii")
    tokenised_filepath = dirpath / "J14.tok"
    _run([*OAKNUT_BASIC, "tokenise", str(source_filepath), str(tokenised_filepath)])
    _run([*OAKNUT_DISC, "put", "--load", "0x1900", "--exec", "0x8023",
          f"{disc_filepath}:$.J14", str(tokenised_filepath)])
    return disc_filepath


@pytest.fixture(scope="module")
def bbc_joystick(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
    dfs_1770_rom_filepath: Path,
    joystick_disc_filepath: Path,
) -> Iterator[Beebium]:
    """A Model B with a joystick, a 1770, DFS 2.26 and the program in drive 0."""
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server=beebium_server_filepath,
            extra_args=[
                "--joystick",
                "--fdc",
                "acorn-1770",
                "--sideways",
                f"slot=14:type=rom:image={dfs_1770_rom_filepath}",
            ],
            startup_timeout=20.0,
        ) as bbc:
            bbc.disc.drive(0).insert(joystick_disc_filepath, write_protect=True)
            bbc.basic.wait_for_prompt(timeout=20.0)
            yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def test_a_joystick_set_through_the_service_is_read_by_basic(bbc_joystick: Beebium) -> None:
    controllers = bbc_joystick.controllers.list()
    assert len(controllers) == 1
    joystick = controllers[0]
    assert joystick.controller_id == "joystick"
    assert joystick.display_name == "Joystick"
    assert [(c.id, c.kind) for c in joystick.controls] == [
        ("stick1.x", ControlKind.AXIS),
        ("stick1.y", ControlKind.AXIS),
        ("fire1", ControlKind.BUTTON),
        ("stick2.x", ControlKind.AXIS),
        ("stick2.y", ControlKind.AXIS),
        ("fire2", ControlKind.BUTTON),
    ]

    bbc_joystick.controllers.set("joystick", {"stick1.x": 0xF000, "fire1": True})
    assert bbc_joystick.controllers.get("joystick") == {
        "stick1.x": 0xF000,
        "stick1.y": 32768,
        "fire1": True,
        "stick2.x": 32768,
        "stick2.y": 32768,
        "fire2": False,
    }

    bbc_joystick.keyboard.type('CHAIN "J14"\r')
    bbc_joystick.basic.wait_for_text(" END14", timeout=30.0)
    screen = bbc_joystick.video.screen_text().text
    match = re.search(r"JOY14 (\S+) (\S+) END14", screen)
    assert match, screen
    adval_1 = int(match.group(1), 16)
    assert 0xF000 <= adval_1 <= 0xF0FF
    assert match.group(2) == "1"


def test_an_unknown_control_is_refused_with_the_servers_sentence(
    bbc_joystick: Beebium,
) -> None:
    before = bbc_joystick.controllers.get("joystick")
    with pytest.raises(grpc.RpcError) as excinfo:
        bbc_joystick.controllers.set("joystick", {"stick1.y": 0, "stick3.x": 0})
    assert excinfo.value.code() == grpc.StatusCode.INVALID_ARGUMENT
    assert excinfo.value.details() == "The controller joystick has no control named stick3.x."
    assert bbc_joystick.controllers.get("joystick") == before
