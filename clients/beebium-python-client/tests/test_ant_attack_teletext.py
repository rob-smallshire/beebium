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

"""Ant Attack's teletext pages read and draw correctly with the interlace off.

The BBC conversion of Ant Attack (a work in progress by its author) shows its
title page in MODE 7 with the 6845's interlace turned off -- R8=&92, R9=9 -- and
plays on a split screen, a MODE 4 view above a teletext panel, switched twice a
frame by a User VIA timer. The SAA5050 counts a character's ten lines itself,
from VSYNC and display enable, so both must draw whole glyphs and read back as
text. Reading the row address as the glyph line instead drew the top half of
every character and lost the text, so the app found nothing to copy.

The disc is third party and is never committed. Point BEEBIUM_ANT_ATTACK_DISC
at it, or place it in the (git-ignored) discs/games/antattack.ssd; otherwise the
test skips. It always runs from a temporary copy. The committed regression
coverage is the guest-programmed C++ tests in tests/test_saa5050_line_counter.cpp.

The game wants sideways RAM in slot 4 ("No sideways RAM in bank 4!" otherwise),
so it runs on the ROM/RAM board preset with slot 4 configured as RAM.
"""

from __future__ import annotations

import os
import shutil
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.screen import screen_contains

ANT_ATTACK_DISC_ENV = "BEEBIUM_ANT_ATTACK_DISC"
ANT_ATTACK_DISC_FILENAME = "antattack.ssd"

INSTRUCTION_PAGES = ["After a long journey", "Jump on an ant", "Game Controls"]
TITLE_PROMPT = "Girl or Boy (g/b) ?"
PRESS_ANY_KEY = "PRESS ANY KEY"
PLAY_PANEL_TEXT = ["SCORE", "Rescued", "AMMO", "READY WHEN YOU ARE"]


@pytest.fixture(scope="module")
def ant_attack_disc_filepath() -> Path:
    """The Ant Attack disc, from the environment or the git-ignored games folder."""
    candidates = []
    if os.environ.get(ANT_ATTACK_DISC_ENV):
        candidates.append(Path(os.environ[ANT_ATTACK_DISC_ENV]))
    repo_root = Path(__file__).parents[3]
    candidates.append(repo_root / "discs" / "games" / ANT_ATTACK_DISC_FILENAME)
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    pytest.skip(f"Ant Attack disc not found: set {ANT_ATTACK_DISC_ENV} to run this test")


@pytest.fixture
def bbc_ant_attack(launch_bbc, ant_attack_disc_filepath: Path, tmp_path: Path) -> Beebium:
    disc_filepath = tmp_path / ANT_ATTACK_DISC_FILENAME
    shutil.copyfile(ant_attack_disc_filepath, disc_filepath)
    bbc = launch_bbc(
        preset="model-b-romram-disc",
        variant="model-b-romram",
        extra_args=[
            "--sideways",
            "slot=4:type=ram",
            "--auto-boot",
            "--floppy",
            f"0:{disc_filepath}",
        ],
        startup_timeout=30.0,
    )
    bbc.debugger.stop()
    return bbc


def _run_until_on_screen(bbc: Beebium, text: str, emulated_seconds: float = 180.0) -> None:
    """Run the machine until ``text`` is in MODE 7 screen memory."""
    found = bbc.run_until_or_timeout(
        lambda: screen_contains(bbc, text),
        emulated_seconds=emulated_seconds,
        chunk_seconds=0.5,
    )
    assert found, f"{text!r} never appeared in screen memory"


def _advance_to_title(bbc: Beebium) -> None:
    for page in INSTRUCTION_PAGES:
        _run_until_on_screen(bbc, page)
        bbc.keyboard.type(" ")
    _run_until_on_screen(bbc, TITLE_PROMPT)
    bbc.run_for_emulated_seconds(1.0)
    assert not screen_contains(bbc, "No sideways RAM"), "slot 4 is not sideways RAM"


class TestAntAttackTeletext:
    def test_title_page_reads_with_the_interlace_off(self, bbc_ant_attack: Beebium) -> None:
        bbc = bbc_ant_attack
        _advance_to_title(bbc)

        # The page this test exists for: teletext with the interlace off.
        bbc.debugger.ensure_stopped()
        registers = bbc.crtc.state.registers
        assert registers[8] & 0x03 in (0, 2), f"R8=&{registers[8]:02X} is interlaced"
        assert registers[9] == 9, f"R9={registers[9]}, not ten lines per row"
        assert bbc.video_ula.state.control & 0x02, "the Video ULA is not showing teletext"

        # What the app's Copy reads: the page, row by row, by glyph.
        assert TITLE_PROMPT in bbc.video.screen_text().text

    def test_play_panel_reads_on_the_split_screen(self, bbc_ant_attack: Beebium) -> None:
        bbc = bbc_ant_attack
        _advance_to_title(bbc)
        bbc.keyboard.type("g")
        _run_until_on_screen(bbc, PRESS_ANY_KEY)
        bbc.run_for_emulated_seconds(0.5)
        bbc.keyboard.type(" ")
        bbc.run_for_emulated_seconds(6.0)

        # A bitmap band above a teletext band.
        frame = bbc.video.capture_frame()
        assert len(frame.regions) >= 2, f"no split screen: {frame.regions}"

        text = bbc.video.screen_text().text
        for expected in PLAY_PANEL_TEXT:
            assert expected in text, f"{expected!r} not read from the teletext panel"

    def test_selection_over_the_panel_reads_the_line_under_it(self, bbc_ant_attack: Beebium) -> None:
        # The teletext panel starts 128 lines down, part way through one of the
        # SAA5050's character rows, so its first whole row (SCORE) begins two
        # lines below the band's top. A selection over the SCORE line's pixels
        # must read SCORE, not the partial row above it.
        bbc = bbc_ant_attack
        _advance_to_title(bbc)
        bbc.keyboard.type("g")
        _run_until_on_screen(bbc, PRESS_ANY_KEY)
        bbc.run_for_emulated_seconds(0.5)
        bbc.keyboard.type(" ")
        bbc.run_for_emulated_seconds(6.0)
        bbc.debugger.ensure_stopped()

        frame = bbc.video.capture_frame()
        assert len(frame.regions) >= 2, f"no split screen: {frame.regions}"
        band_top = frame.regions[1].start_line

        # The grid of the band begins where the chip's row began, above the band.
        bands = bbc.video.screen_geometry().bands
        panel = next(band for band in bands if band.top == band_top)
        assert panel.origin_y < panel.top, (panel.origin_y, panel.top)
        assert (panel.top - panel.origin_y) < panel.row_pitch

        # Find the SCORE line in the pixels themselves: the first lit scan line
        # of the panel.
        stride = frame.width * 4

        def lit(y: int) -> bool:
            row = frame.pixels[y * stride : (y + 1) * stride]
            return any(row[i] or row[i + 1] or row[i + 2] for i in range(0, len(row), 4))

        first_lit = next(y for y in range(band_top, frame.height) if lit(y))
        selected = bbc.video.screen_text(region=(0, first_lit, frame.width, 3)).text
        assert "SCORE" in selected, f"selection at line {first_lit} read {selected!r}"
