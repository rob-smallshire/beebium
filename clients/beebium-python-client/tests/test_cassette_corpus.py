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

"""Commercial tapes that stress the cassette path, loaded with boot_tape (#208).

The tapes are third-party images, never committed. They are read from the
directory BEEBIUM_TAPE_CORPUS_DIRPATH names (beebjit's test/tapes is one such
collection) and identified by SHA-256, so a file of the same name with other
contents is not mistaken for the tape. Each test SKIPs when the directory or the
tape is absent.

Each tape is loaded through MOS 1.20 as a test vector and run, in emulated time
stepped by the debugger, until its first screen of play shows a string read from
the screen. The strings were recorded by watching each tape load once. Most of
these games draw in bitmap modes, which is why the test reads the screen through
the server's screen text rather than the teletext grid.
"""

from __future__ import annotations

import hashlib
import os
from dataclasses import dataclass
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError

_CORPUS_ENV = "BEEBIUM_TAPE_CORPUS_DIRPATH"


@dataclass(frozen=True)
class CorpusTape:
    title: str
    sha256: str
    command: str          # typed after *TAPE
    screen_text: str      # shown on the first screen of play
    seconds: int          # emulated seconds allowed to reach it
    stresses: str


_TAPES = [
    CorpusTape("Caveman Capers", "82d9a56d964ee0d2f8840fdb07232d9d6e15e20c7e9304b28070c2547a74f4f7",
               'CHAIN""', "Press 'SPACE' To Start", 420,
               "the 6850's /DCD latch and its clearing"),
    CorpusTape("Star Drifter", "648b11f0dabc9d5502c8fd78fbebb07090363d66a03f4dc47b215bffa2ad0ba4",
               'CHAIN""', "PLEASE SELECT YOUR KEYS", 420,
               "/DCD after very short carrier tones"),
    CorpusTape("Atic Atac", "4d0d02fde0334503eff8df956dcfc1644ea68235ab2dec057420cb3272ab7fc1",
               "*RUN", "SELECTION SHEET", 540,
               "the motor toggled during carrier"),
    CorpusTape("Uridium (CSW)", "6db5af3d6cdd4fd154c90cbdb2b53e103eb765e171aa83df8889cf026d10969f",
               'CHAIN""', "2. Keyboard", 180,
               "tone, data, tone, data with no gap: /DCD must rise again"),
    CorpusTape("Frak", "096c755e69c9929033e0f80e16dde08758fe390f8940d5dde8f6e746efef1bdc",
               'CHAIN""', "Grandmaster Frak", 460,
               "the 6850's master reset clearing its control register"),
    # The game's own font is not read as text; the ropes of the ring are.
    CorpusTape("Pro Boxing Simulator", "87136038981e361fee5b7fcaa9c4b0640a09f4a7258dbc645bcc79515f590b70",
               'CHAIN""', "_______  _______", 460,
               "bytes written to the 6850 during and after the load"),
]


def _corpus_dirpath() -> Path:
    value = os.environ.get(_CORPUS_ENV)
    if not value:
        pytest.skip(f"{_CORPUS_ENV} is not set, so the commercial tape corpus is not available.")
    dirpath = Path(value)
    if not dirpath.is_dir():
        pytest.skip(f"{_CORPUS_ENV} names {dirpath}, which is not a directory.")
    return dirpath


def _find_tape(dirpath: Path, sha256: str) -> Path | None:
    for filepath in sorted(dirpath.iterdir()):
        if filepath.is_file() and hashlib.sha256(filepath.read_bytes()).hexdigest() == sha256:
            return filepath
    return None


@pytest.mark.parametrize("tape", _TAPES, ids=[t.title for t in _TAPES])
def test_corpus_tape_reaches_its_first_screen(tape: CorpusTape, mos_filepath, basic_filepath,
                                              beebium_server_filepath):
    tape_filepath = _find_tape(_corpus_dirpath(), tape.sha256)
    if tape_filepath is None:
        pytest.skip(f"The corpus has no copy of {tape.title} with the expected SHA-256.")
    try:
        with Beebium.launch(mos_filepath=mos_filepath, basic_filepath=basic_filepath,
                            server=beebium_server_filepath) as bbc:
            bbc.boot_tape(tape_filepath, command=tape.command)
            bbc.debugger.stop()
            text = ""
            for _ in range(tape.seconds):
                bbc.debugger.step_cycles(2_000_000)
                text = bbc.video.screen_text().text
                if tape.screen_text in text:
                    break
            assert tape.screen_text in text, (
                f"{tape.title} ({tape.stresses}) did not reach its first screen in "
                f"{tape.seconds} emulated seconds; the screen reads:\n{text}")
    except ServerNotFoundError as e:
        pytest.skip(str(e))
