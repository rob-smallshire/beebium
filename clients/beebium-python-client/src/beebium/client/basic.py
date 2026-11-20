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

"""BBC BASIC workflow helpers for the beebium client.

These helpers make it easier to work with BBC BASIC in tests by providing
high-level functions for common operations like waiting for prompts,
running programs, and reading screen output.
"""

from __future__ import annotations

import time
from typing import TYPE_CHECKING

from beebium.client._repr import facade_repr
from beebium.client.exceptions import ScreenTextUnavailableError, TimeoutError
from beebium.client.screen import _cells_of, _displayed_rows, cells_to_text, read_mode7_screen

if TYPE_CHECKING:
    from beebium.client import Beebium
    from beebium.client.video_ula import VideoUlaState


# The bitmap modes a Video ULA control register shows, by its fast clock and
# characters a line (MODES 3 and 6 differ from 0 and 4 only in the 6845).
_BITMAP_MODES = {
    (True, 80): "MODE 0 or 3",
    (True, 40): "MODE 1",
    (True, 20): "MODE 2",
    (False, 40): "MODE 4 or 6",
    (False, 20): "MODE 5",
}


def _bitmap_mode_name(state: VideoUlaState) -> str:
    characters = (10, 20, 40, 80)[state.line_width_mode]
    return _BITMAP_MODES.get(
        (state.fast_clock, characters),
        f"a bitmap mode of {characters} characters a line",
    )

# The BBC BASIC prompt character, expected at the start of a line.
PROMPT = ">"


class Basic:
    """BBC BASIC workflow helpers.

    Provides high-level functions for working with BBC BASIC.

    Usage:
        # Wait for the BASIC prompt
        bbc.basic.wait_for_prompt()

        # Run a program
        bbc.basic.run_program("10 PRINT \"HELLO\"\\n20 END")

        # Read screen text
        text = bbc.basic.read_screen_text(0, 5)
    """

    def __init__(self, client: Beebium):
        """Create a BASIC helper.

        Args:
            client: The Beebium client to use.
        """
        self._client = client

    def __repr__(self) -> str:
        return facade_repr(
            self, f"{self._client.target}: BBC BASIC helpers (type, run and read programs)"
        )

    def wait_for_prompt(
        self,
        timeout: float = 5.0,
        poll_interval: float = 0.01,
    ) -> bool:
        """Wait for the BASIC '>' prompt to appear.

        Looks for the prompt character in Mode 7 screen memory.

        Args:
            timeout: Maximum time to wait (seconds).
            poll_interval: How often to check (seconds).

        Returns:
            True if the prompt was found.

        Raises:
            TimeoutError: If the prompt doesn't appear within timeout.
        """
        deadline = time.monotonic() + timeout

        while time.monotonic() < deadline:
            # The prompt appears at the start of a line in the MODE 7 display;
            # until there is one (None), there is no prompt yet.
            rows = _displayed_rows(self._client) or []
            if any(row.startswith(PROMPT) for row in rows):
                return True

            time.sleep(poll_interval)

        raise TimeoutError(f"BASIC prompt not found within {timeout} seconds")

    def wait_for_text(
        self,
        text: str,
        timeout: float = 5.0,
        poll_interval: float = 0.01,
        case_sensitive: bool = True,
    ) -> bool:
        """Wait for specific text to appear on screen.

        Args:
            text: The text to look for.
            timeout: Maximum time to wait (seconds).
            poll_interval: How often to check (seconds).
            case_sensitive: Whether the search is case-sensitive.

        Returns:
            True if the text was found.

        Raises:
            TimeoutError: If the text doesn't appear within timeout.
        """
        deadline = time.monotonic() + timeout
        search_text = text if case_sensitive else text.upper()

        while time.monotonic() < deadline:
            screen = self.read_screen_text()
            screen_text = screen if case_sensitive else screen.upper()

            if search_text in screen_text:
                return True

            time.sleep(poll_interval)

        raise TimeoutError(f"Text '{text}' not found within {timeout} seconds")

    def read_screen_text(self, start_row: int = 0, end_row: int | None = None) -> str:
        """Read the text on the displayed screen, in whatever mode it is in.

        The server says what is displayed, and the text is read accordingly:

        - a teletext (MODE 7) display from the grid the SAA5050 drew, however
          many rows it has, so a program that reprograms the 6845 still reads;
        - a teletext display that has not drawn a frame yet (a machine still
          booting) as blank rows, so a caller can poll it;
        - a bitmap display (MODES 0-6) through the server's screen-text
          reading, :meth:`Video.screen_text`, one line per character row.

        Args:
            start_row: First row to read (0-based).
            end_row: Row to stop before. None reads to the last row; a row
                past the last is clipped to it.

        Returns:
            The screen text as a string with newlines between rows.

        Raises:
            ScreenTextUnavailableError: The display is one the server cannot
                read text from; the message names its mode and says to
                capture a frame instead.
        """
        rows = self._displayed_text_rows()
        stop = len(rows) if end_row is None else min(end_row, len(rows))
        return "\n".join(row.rstrip() for row in rows[max(0, start_row) : stop])

    def _displayed_text_rows(self) -> list[str]:
        teletext = self._client.video.teletext_screen()
        if teletext.active:
            return cells_to_text(_cells_of(teletext))
        control = self._client.video_ula.state
        if control.teletext_mode:
            # Teletext with no frame drawn yet: blank, so a boot can be polled.
            return read_mode7_screen(self._client)
        reading = self._client.video.screen_text(search="aligned")
        if reading.supported:
            return reading.text.split("\n")
        raise ScreenTextUnavailableError(
            f"The screen is in {_bitmap_mode_name(control)} (Video ULA control "
            f"0x{control.control:02X}), which this server cannot read text from; "
            f"capture it with bbc.video.capture_frame() instead."
        )

    def run_program(
        self,
        source: str,
        wait_for_completion: bool = True,
        completion_timeout: float = 10.0,
    ) -> None:
        """Type and run a BASIC program.

        Args:
            source: The BASIC program source code.
            wait_for_completion: Whether to wait for the program to finish.
            completion_timeout: Maximum time to wait for completion (seconds).
        """
        # Wait for the prompt first
        self.wait_for_prompt()

        # Type NEW to clear any existing program
        self._client.keyboard.type("NEW")
        self._client.keyboard.press_return()
        time.sleep(0.1)

        # Type each line of the program
        for line in source.strip().split("\n"):
            self._client.keyboard.type(line.strip())
            self._client.keyboard.press_return()
            time.sleep(0.05)

        # Wait for prompt after entering program
        self.wait_for_prompt()

        # Run the program
        self._client.keyboard.type("RUN")
        self._client.keyboard.press_return()

        if wait_for_completion:
            # Wait for the prompt to reappear (program finished)
            time.sleep(0.1)  # Give program time to start
            self.wait_for_prompt(timeout=completion_timeout)

    def type_and_enter(self, text: str, enter_delay: float = 0.05) -> None:
        """Type text and press RETURN.

        Args:
            text: The text to type.
            enter_delay: Delay before pressing RETURN (seconds).
        """
        self._client.keyboard.type(text)
        time.sleep(enter_delay)
        self._client.keyboard.press_return()

    def send_break(self, hold_time: float = 0.1) -> None:
        """Send a BREAK keypress.

        Args:
            hold_time: How long to hold the BREAK key, in emulated seconds.
                The machine must be running.
        """
        self._client.keyboard.press_break(hold_time=hold_time)

    def get_himem(self) -> int:
        """Get the current HIMEM value.

        Returns:
            The HIMEM address (top of BASIC memory).
        """
        # HIMEM is stored at &0006-&0007 (little-endian)
        return self._client.memory.address.bus.cast("<H")[0x0006]

    def get_page(self) -> int:
        """Get the current PAGE value.

        Returns:
            The PAGE address (start of BASIC program area).
        """
        # PAGE is stored at &0018-&0019 (little-endian)
        return self._client.memory.address.bus.cast("<H")[0x0018]

    def get_top(self) -> int:
        """Get the current TOP value.

        Returns:
            The TOP address (end of current BASIC program).
        """
        # TOP is stored at &0012-&0013 (little-endian)
        return self._client.memory.address.bus.cast("<H")[0x0012]
