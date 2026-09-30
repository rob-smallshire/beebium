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

from beebium.client.exceptions import TimeoutError
from beebium.client.screen import read_mode7_screen

if TYPE_CHECKING:
    from beebium.client import Beebium


# Screen memory addresses for each mode
# Format: (base_address, bytes_per_line, lines, chars_per_line)
SCREEN_MODES = {
    0: (0x3000, 640, 32, 80),  # 640x256 2-colour, 80 chars
    1: (0x3000, 320, 32, 40),  # 320x256 4-colour, 40 chars
    2: (0x3000, 160, 32, 20),  # 160x256 16-colour, 20 chars
    3: (0x4000, 640, 25, 80),  # 640x200 2-colour (text), 80 chars
    4: (0x5800, 320, 32, 40),  # 320x256 2-colour, 40 chars
    5: (0x5800, 160, 32, 20),  # 160x256 4-colour, 20 chars
    6: (0x6000, 320, 25, 40),  # 320x200 2-colour (text), 40 chars
    7: (0x7C00, 40, 25, 40),  # Teletext mode, 40 chars
}

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
            # The prompt appears at the start of a line in the MODE 7 display.
            rows = read_mode7_screen(self._client)
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

    def read_screen_text(
        self,
        start_row: int = 0,
        end_row: int | None = None,
        mode: int = 7,
    ) -> str:
        """Read text from screen memory.

        Args:
            start_row: First row to read (0-based).
            end_row: Last row to read (exclusive). None means all rows.
            mode: Screen mode (default 7 for teletext).

        Returns:
            The screen text as a string with newlines between rows.
        """
        if mode not in SCREEN_MODES:
            raise ValueError(f"Unsupported mode: {mode}")

        _, _, lines, _ = SCREEN_MODES[mode]

        if end_row is None:
            end_row = lines

        start_row = max(0, min(start_row, lines - 1))
        end_row = max(start_row + 1, min(end_row, lines))

        if mode == 7:
            # Teletext: read what is actually displayed, corrected for scroll.
            rows = read_mode7_screen(self._client)
        else:
            # Graphics modes: pixel decoding to characters is not implemented.
            rows = ["[graphics mode - not implemented]"] * lines

        result = [rows[row].rstrip() for row in range(start_row, end_row)]
        return "\n".join(result)

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
