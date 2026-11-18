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

"""The uPD7002 analogue-to-digital converter seen from BBC BASIC (#98).

Tests 18 and 19 of docs/discussion/upd7002-adc-specification.md. The BASIC
programs are tokenised with oaknut-basic and put on a disc image with
oaknut-disc, then CHAINed; nothing is typed in but the CHAIN.

There is no Python-side way to drive the converter's inputs in this round:
the inputs are unplugged and read the unplugged level, &CAFE. The C++
tests cover set input levels; a later round gives the Python client a real
input source through the analogue port.
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

# ADVAL(1) to ADVAL(4), and the fire buttons in ADVAL(0)'s bottom bits.
ADVAL_PROGRAM = """\
10 *FX16,4
20 T%=TIME:REPEAT UNTIL TIME>T%+30
30 PRINT "ADC18 ";~ADVAL(1);" ";~ADVAL(2);" ";~ADVAL(3);" ";~ADVAL(4);" ";ADVAL(0) AND 3;" END18"
"""

# The status register sampled once a centisecond while the operating system
# cycles the channels: how often it reads the old open-bus value &20, which
# channels appear, and how often the chip is not busy.
STATUS_PROGRAM = """\
10 DIM C%(3):N%=0:B%=0:M%=0
20 FOR I%=1 TO 100
30 T%=TIME:REPEAT UNTIL TIME<>T%
40 S%=?&FEC0
50 IF S%=&20 THEN N%=N%+1
60 C%(S% AND 3)=C%(S% AND 3)+1
70 IF S% AND &40 THEN B%=B%+1
80 IF S% AND &08 THEN M%=M%+1
90 NEXT
100 PRINT "ADC19 ";N%;" ";C%(0);" ";C%(1);" ";C%(2);" ";C%(3);" ";B%;" ";M%;" END19"
"""


def _run(args: list[str]) -> None:
    subprocess.run(args, check=True, capture_output=True)


@pytest.fixture(scope="module")
def adc_disc_filepath(tmp_path_factory: pytest.TempPathFactory) -> Path:
    if shutil.which("uvx") is None:
        pytest.skip("uvx (from uv) is needed to build the BASIC disc")
    dirpath = tmp_path_factory.mktemp("adc-disc")
    disc_filepath = dirpath / "adc.ssd"
    _run([*OAKNUT_DISC, "create", str(disc_filepath), "--title", "ADC"])
    for name, text in (("A18", ADVAL_PROGRAM), ("A19", STATUS_PROGRAM)):
        source_filepath = dirpath / f"{name}.bas"
        source_filepath.write_text(text, encoding="ascii")
        tokenised_filepath = dirpath / f"{name}.tok"
        _run([*OAKNUT_BASIC, "tokenise", str(source_filepath), str(tokenised_filepath)])
        _run([*OAKNUT_DISC, "put", "--load", "0x1900", "--exec", "0x8023",
              f"{disc_filepath}:$.{name}", str(tokenised_filepath)])
    return disc_filepath


@pytest.fixture(scope="module")
def bbc_adc(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
    dfs_1770_rom_filepath: Path,
    adc_disc_filepath: Path,
) -> Iterator[Beebium]:
    """A Model B with a 1770, DFS 2.26 and the ADC programs in drive 0."""
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server=beebium_server_filepath,
            extra_args=[
                "--fdc",
                "acorn-1770",
                "--sideways",
                f"slot=14:type=rom:image={dfs_1770_rom_filepath}",
            ],
            startup_timeout=20.0,
        ) as bbc:
            bbc.disc.drive(0).insert(adc_disc_filepath, write_protect=True)
            bbc.basic.wait_for_prompt(timeout=20.0)
            yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def _chain_and_read(bbc: Beebium, program: str, number: int) -> list[str]:
    """CHAIN a program and return the fields of its result line, which
    starts ADC<number> and ends END<number>."""
    bbc.keyboard.type(f'CHAIN "{program}"\r')
    bbc.basic.wait_for_text(f" END{number}", timeout=30.0)
    screen = bbc.video.screen_text().text
    match = re.search(rf"ADC{number} (.*?) END{number}", screen)
    assert match, screen
    return match.group(1).split()


def test_unplugged_channels_read_the_unplugged_level_and_no_fire_button(bbc_adc: Beebium) -> None:
    """ADVAL(1) to ADVAL(4) read &CAF0, the twelve bits of the unplugged
    level, and ADVAL(0) AND 3 reads 0: no fire button. Nothing drives the
    inputs from Python in this round; the C++ tests cover driven levels."""
    fields = _chain_and_read(bbc_adc, "A18", 18)
    assert fields == ["CAF0", "CAF0", "CAF0", "CAF0", "0"]


def test_the_status_register_reads_the_chip_cycling_the_channels(bbc_adc: Beebium) -> None:
    """&FEC0 now reads the converter, never the open-bus &20 an unmapped
    read gave. The operating system converts in 12-bit mode and cycles the
    four channels, so all four appear. It starts the next conversion in the
    same interrupt that collects a result, so the chip is busy almost all
    the time and bit 6 (not busy) is seldom seen; it is not asserted."""
    open_bus, *per_channel, not_busy, twelve_bit = (
        int(field) for field in _chain_and_read(bbc_adc, "A19", 19))
    assert open_bus == 0
    assert all(count > 0 for count in per_channel), per_channel
    assert sum(per_channel) == 100
    assert twelve_bit == 100
    assert not_busy < 100
