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

"""Integration tests for the keyboard API.

Tests that the cycle-paced typing mechanism works reliably via the
TypeAheadQueue. All tests use emulator cycle counts for timing, never
wall-clock sleeps.
"""

from __future__ import annotations

import time

import pytest

from beebium.client import Beebium
from beebium.client.screen import screen_contains

# BBC Micro host clock: 2 MHz
HOST_CLOCK_HZ = 2_000_000


def _run_for_emulated_seconds(bbc: Beebium, seconds: float) -> None:
    """Run the emulator for the given number of emulated seconds."""
    clock_hz = bbc.system.clock_speed_hz or HOST_CLOCK_HZ
    target_cycles = int(seconds * clock_hz)
    start = bbc.debugger.cycle_count
    with bbc.debugger.running():
        while bbc.debugger.cycle_count - start < target_cycles:
            time.sleep(0.05)


def _wait_for_screen(bbc: Beebium, text: str, emulated_seconds: float = 5.0) -> bool:
    """Run until text appears on screen or timeout."""
    clock_hz = bbc.system.clock_speed_hz or HOST_CLOCK_HZ
    cycle_budget = int(emulated_seconds * clock_hz)
    start = bbc.debugger.cycle_count
    target = start + cycle_budget

    with bbc.debugger.running():
        while True:
            time.sleep(0.05)
            current = bbc.debugger.cycle_count
            if current >= target:
                return screen_contains(bbc, text)
            if current - start >= clock_hz:
                bbc.debugger.stop()
                if screen_contains(bbc, text):
                    return True
                bbc.debugger.run()


class TestType:
    """Tests for Keyboard.type() — the cycle-paced typing mechanism."""

    def test_type_mixed_case(self, bbc: Beebium) -> None:
        """Mixed case text is typed correctly."""
        _run_for_emulated_seconds(bbc, 2.0)
        bbc.debugger.stop()
        bbc.debugger.run()
        # At BASIC prompt, input is uppercased, so verify BASIC sees it
        bbc.keyboard.type("PRINT 2+3\r")
        assert _wait_for_screen(bbc, "5")

    def test_type_uppercase(self, bbc: Beebium) -> None:
        """Uppercase letters (requiring SHIFT) appear on screen."""
        _run_for_emulated_seconds(bbc, 2.0)
        bbc.debugger.stop()
        bbc.debugger.run()
        bbc.keyboard.type("PRINT")
        assert _wait_for_screen(bbc, "PRINT")

    def test_type_digits(self, bbc: Beebium) -> None:
        """Digits are typed correctly."""
        _run_for_emulated_seconds(bbc, 2.0)
        bbc.debugger.stop()
        bbc.debugger.run()
        bbc.keyboard.type("12345")
        assert _wait_for_screen(bbc, "12345")

    def test_type_with_return(self, bbc: Beebium) -> None:
        """Typing with \\r executes a BASIC command."""
        _run_for_emulated_seconds(bbc, 2.0)
        bbc.debugger.stop()
        bbc.debugger.run()
        bbc.keyboard.type("PRINT 42\r")
        assert _wait_for_screen(bbc, "42")

    def test_type_returns_pending_count(self, bbc: Beebium) -> None:
        """type() returns the number of pending characters."""
        _run_for_emulated_seconds(bbc, 2.0)
        bbc.debugger.stop()
        pending = bbc.keyboard.type("ABC")
        assert pending >= 3

    def test_type_unmappable_raises(self, bbc: Beebium) -> None:
        """Unmappable characters raise ValueError."""
        with pytest.raises(ValueError):
            bbc.keyboard.type("\x00")

    def test_type_empty_string(self, bbc: Beebium) -> None:
        """Empty string is accepted without error."""
        _run_for_emulated_seconds(bbc, 2.0)
        bbc.debugger.stop()
        pending = bbc.keyboard.type("")
        assert pending >= 0


class TestTypeTranslation:
    """Tests for host text translation on Keyboard.type()."""

    def test_crlf_presses_return_once(self, bbc: Beebium) -> None:
        """CRLF is one line ending, not two RETURN presses."""
        _run_for_emulated_seconds(bbc, 2.0)
        bbc.debugger.stop()
        bbc.keyboard.clear_typing()
        pending = bbc.keyboard.type("AB\r\nCD")
        # A, B, RETURN, C, D
        assert pending == 5

    def test_untranslated_crlf_presses_return_twice(self, bbc: Beebium) -> None:
        """Without translation CR and LF are separate RETURN presses."""
        _run_for_emulated_seconds(bbc, 2.0)
        bbc.debugger.stop()
        bbc.keyboard.clear_typing()
        pending = bbc.keyboard.type("AB\r\nCD", translate=False)
        # A, B, RETURN, RETURN, C, D
        assert pending == 6

    def test_lone_newline_is_return(self, bbc: Beebium) -> None:
        """A UNIX line ending types RETURN, so host text needs no fixing up."""
        _run_for_emulated_seconds(bbc, 2.0)
        bbc.debugger.stop()
        bbc.debugger.run()
        bbc.keyboard.type("PRINT 6*7\n")
        assert _wait_for_screen(bbc, "42")

    def test_typographic_quotes_are_translated(self, bbc: Beebium) -> None:
        """Smart quotes from a web page or word processor become ASCII."""
        _run_for_emulated_seconds(bbc, 2.0)
        bbc.debugger.stop()
        bbc.debugger.run()
        bbc.keyboard.type("PRINT “HI”\r")
        assert _wait_for_screen(bbc, "HI")

    def test_untranslated_typographic_quotes_are_rejected(self, bbc: Beebium) -> None:
        """Without translation a smart quote cannot be typed at all."""
        with pytest.raises(ValueError):
            bbc.keyboard.type("“HI”", translate=False)

    def test_rejection_names_the_offending_character(self, bbc: Beebium) -> None:
        """A refusal says which character was at fault."""
        with pytest.raises(ValueError, match=r"U\+2603"):
            bbc.keyboard.type("AB☃CD")


class TestPressReturn:
    """Tests for Keyboard.press_return()."""

    def test_press_return_executes_command(self, bbc: Beebium) -> None:
        """press_return() submits the current line."""
        _run_for_emulated_seconds(bbc, 2.0)
        bbc.debugger.stop()
        bbc.debugger.run()
        bbc.keyboard.type("PRINT 99")
        bbc.keyboard.press_return()
        assert _wait_for_screen(bbc, "99")


class TestPressEscape:
    """Tests for Keyboard.press_escape()."""

    def test_press_escape(self, bbc: Beebium) -> None:
        """press_escape() triggers the Escape condition."""
        _run_for_emulated_seconds(bbc, 2.0)
        bbc.debugger.stop()
        bbc.debugger.run()
        bbc.keyboard.press_escape()
        assert _wait_for_screen(bbc, "Escape")


class TestPressDelete:
    """Tests for Keyboard.press_delete()."""

    def test_press_delete_removes_character(self, bbc: Beebium) -> None:
        """press_delete() removes the last typed character."""
        _run_for_emulated_seconds(bbc, 2.0)
        bbc.debugger.stop()
        bbc.debugger.run()
        # Type "AB", delete "B", type "C" — should show "AC" on screen
        bbc.keyboard.type("AB")
        bbc.keyboard.press_delete()
        bbc.keyboard.type("C\r")
        # BASIC will print "Mistake" or similar for "AC" since it's not valid,
        # but the important thing is "AC" appeared as input
        assert _wait_for_screen(bbc, ">AC")


class TestPressSpace:
    """Tests for Keyboard.press_space()."""

    def test_press_space(self, bbc: Beebium) -> None:
        """press_space() inserts a space."""
        _run_for_emulated_seconds(bbc, 2.0)
        bbc.debugger.stop()
        bbc.debugger.run()
        bbc.keyboard.type("PRINT")
        bbc.keyboard.press_space()
        bbc.keyboard.type("42\r")
        assert _wait_for_screen(bbc, "42")


class TestTypingStatus:
    """Tests for typing_status() and clear_typing()."""

    def test_typing_status_idle_initially(self, bbc: Beebium) -> None:
        """Queue starts idle."""
        idle, pending, queued = bbc.keyboard.typing_status()
        assert idle
        assert pending == 0

    def test_typing_status_after_enqueue(self, bbc: Beebium) -> None:
        """Queue shows pending after type()."""
        bbc.debugger.stop()
        bbc.keyboard.type("HELLO")
        idle, pending, queued = bbc.keyboard.typing_status()
        assert pending >= 5

    def test_clear_typing(self, bbc: Beebium) -> None:
        """clear_typing() empties the queue."""
        bbc.debugger.stop()
        bbc.keyboard.type("HELLO WORLD")
        cleared = bbc.keyboard.clear_typing()
        assert cleared > 0
        idle, pending, queued = bbc.keyboard.typing_status()
        assert pending == 0

    def test_queue_drains_when_running(self, bbc: Beebium) -> None:
        """Queue eventually empties when the emulator runs."""
        _run_for_emulated_seconds(bbc, 2.0)
        bbc.debugger.stop()
        bbc.keyboard.type("HI\r")
        bbc.debugger.run()
        # Wait for queue to drain (5 chars at 100000 cycles each = 0.25s emulated)
        _run_for_emulated_seconds(bbc, 1.0)
        idle, pending, queued = bbc.keyboard.typing_status()
        assert idle
        assert pending == 0
