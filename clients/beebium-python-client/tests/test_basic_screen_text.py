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

"""basic.read_screen_text() reads the display in whatever mode it is in.

A teletext display is read from the grid the SAA5050 drew, however many rows
it has; a bitmap display through the server's screen-text reading; and a
display the server cannot read raises a sentence naming the mode and what to
call instead, never an IndexError.
"""

from __future__ import annotations

from types import SimpleNamespace

import pytest

from beebium.client import Beebium
from beebium.client.basic import Basic
from beebium.client.exceptions import ScreenTextUnavailableError
from beebium.client.video_ula import VideoUlaState

MODE_4_CONTROL = 0x88
TELETEXT_CONTROL = 0x4B


class FakeTeletextScreen:
    def __init__(self, rows: list[str], *, active: bool = True):
        self.active = active
        self.rows = len(rows)
        self.columns = 40
        self._rows = [row.ljust(40) for row in rows]

    def cell(self, row: int, column: int):
        return SimpleNamespace(character=ord(self._rows[row][column]))


def fake_client(*, teletext, control: int, screen_text=None):
    video = SimpleNamespace(
        teletext_screen=lambda: teletext,
        screen_text=lambda **kwargs: screen_text,
    )
    video_ula = SimpleNamespace(state=VideoUlaState(control=control, palette=(0,) * 16))
    return SimpleNamespace(video=video, video_ula=video_ula)


class TestTeletext:
    def test_a_short_teletext_grid_is_read_at_its_own_size(self):
        rows = [f"row {n}" for n in range(20)]
        basic = Basic(fake_client(teletext=FakeTeletextScreen(rows), control=TELETEXT_CONTROL))

        text = basic.read_screen_text()

        assert text.splitlines() == rows
        assert basic.read_screen_text(18, 40).splitlines() == ["row 18", "row 19"]

    def test_teletext_before_its_first_frame_is_blank(self):
        teletext = FakeTeletextScreen([""] * 25, active=False)
        basic = Basic(fake_client(teletext=teletext, control=TELETEXT_CONTROL))

        assert basic.read_screen_text() == "\n" * 24


class TestUnreadable:
    def test_a_display_the_server_cannot_read_raises_a_sentence(self):
        teletext = FakeTeletextScreen([""] * 25, active=False)
        unsupported = SimpleNamespace(supported=False, text="")
        basic = Basic(fake_client(teletext=teletext, control=MODE_4_CONTROL, screen_text=unsupported))

        with pytest.raises(ScreenTextUnavailableError) as raised:
            basic.read_screen_text()

        message = str(raised.value)
        assert "MODE 4 or 6" in message
        assert "0x88" in message
        assert "capture_frame" in message
        assert message.endswith(".")


def test_a_bitmap_mode_is_read_through_the_server(bbc: Beebium):
    bbc.basic.wait_for_prompt()
    bbc.keyboard.type('MODE 4\rPRINT "HELLO BITMAP"\r')

    def shows_hello() -> bool:
        try:
            return "HELLO BITMAP" in bbc.basic.read_screen_text()
        except ScreenTextUnavailableError:
            return False

    assert bbc.run_until_or_timeout(shows_hello, 10.0), bbc.video.screen_text().text
    assert not bbc.video_ula.state.teletext_mode
