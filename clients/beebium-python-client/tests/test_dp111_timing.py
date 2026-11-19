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

"""Whole-machine regression guard for issue #81: Dominic Plunkett's (dp111)
6502 instruction timing suite.

The suite (github.com/dp111/6502Timing, GPL-3.0, version 0.24) times almost every
documented and undocumented NMOS 6502 instruction against the 1 MHz System VIA
timer, prints any instruction whose timing is wrong, tallies the failures at zero
page &7A (``passfailzp``), and prints ``Number of failures : 0xNN``. Two images:
``6502timing.ssd`` and ``6502timing1M.ssd``, which places the timed absolute
addresses at &FCFE so the run also exercises Beebium's own 1 MHz bus cycle
stretching.

Both pass on master today, so this is a regression guard for the core, the VIA
timer and the 1 MHz stretch -- not a reproduction of a defect. About four
emulated seconds each. Not checked by the suite, per its README: BRK and the jam
(HALT) instructions.

The printed ``Number of failures`` line is the authoritative result: the suite
prints it straight from &7A, and unlike &7A itself (which the BASIC prompt reuses
once control returns) it cannot be clobbered. &7A is asserted too, read the moment
the line appears.
"""

from __future__ import annotations

import re
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.screen import read_mode7_screen, screen_contains

PASS_FAIL_ZP = 0x7A  # the suite's running failure count

# An error line: the instruction's name, then its timing error as two hex
# digits from column 37.
ERROR_LINE = re.compile(r"^(?P<name>\S.*?)\s+(?P<error>[0-9A-F]{2})\s*$")
CLI_PROBE_HEADING = "Testing CLI ( warning may fail )"


def _error_lines(rows: list[str]) -> list[tuple[str, str]]:
    """The (preceding line, error line) pairs the suite printed for mistimed
    instructions, from its first checking heading onwards."""
    lines = [row.rstrip() for row in rows]
    start = next(
        (i for i, line in enumerate(lines) if line.startswith("Checking documented")),
        len(lines),
    )
    errors = []
    for i in range(start, len(lines)):
        if lines[i].startswith("Number of failures"):
            break
        if ERROR_LINE.match(lines[i]):
            errors.append((lines[i - 1], lines[i]))
    return errors

CASES = [
    pytest.param("6502timing.ssd", id="6502timing"),
    pytest.param("6502timing1M.ssd", id="6502timing1M"),
]


def _disc_filepath(filename: str) -> Path | None:
    repo_root = Path(__file__).parent.parent.parent.parent
    candidate = repo_root / "tests" / "assets" / "discs" / filename
    return candidate if candidate.exists() else None


@pytest.fixture
def bbc_model_b(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
    dfs_1770_rom_filepath: Path,
) -> Beebium:
    """A stock Model B + 1770 DFS booted to BASIC, isolated per test."""
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
            booted = bbc.run_until_or_timeout(
                lambda: screen_contains(bbc, "BASIC"),
                emulated_seconds=15.0,
            )
            if not booted:
                rows = read_mode7_screen(bbc)
                for i, row in enumerate(rows):
                    print(f"Row {i:2d}: [{row}]")
                pytest.fail("BASIC banner did not appear after boot")
            yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))


class TestDp111Timing:
    """Issue #81: dp111's 6502 timing suite passes on the NMOS host."""

    @pytest.mark.parametrize("disc_filename", CASES)
    def test_all_instruction_timings_pass(
        self, bbc_model_b: Beebium, disc_filename: str
    ) -> None:
        disc_filepath = _disc_filepath(disc_filename)
        if disc_filepath is None:
            pytest.skip(f"dp111 timing disc not found: {disc_filename}")
        bbc = bbc_model_b

        bbc.disc.drive(0).insert(disc_filepath)
        bbc.keyboard.type("*RUN 6502tim\r")

        # The suite ends by printing "Number of failures : 0xNN". Wait for that
        # line, then read the count from &7A before the BASIC prompt reclaims it.
        done = bbc.run_until_or_timeout(
            lambda: screen_contains(bbc, "Number of failures"),
            emulated_seconds=20.0,
        )
        failure_zp = bbc.memory.address.peek[PASS_FAIL_ZP]
        rows = read_mode7_screen(bbc)

        if not done:
            print(f"\nScreen ({disc_filename} did not finish):")
            for i, row in enumerate(rows):
                print(f"Row {i:2d}: [{row}]")
        assert done, f"{disc_filename}: the suite did not report a result in time"

        text = "\n".join(rows)
        match = re.search(r"Number of failures\s*:\s*0x([0-9A-Fa-f]{2})", text)
        errors = _error_lines(rows)
        # The CLI probe runs with interrupts enabled, and the suite masks only
        # the User VIA, so a System VIA interrupt such as vsync can land in its
        # timed window, as on real hardware; the suite labels the line advisory.
        # Only that line is tolerated, and only after its own heading.
        strict_errors = [
            line
            for previous, line in errors
            if not (
                previous.strip() == CLI_PROBE_HEADING
                and ERROR_LINE.match(line).group("name") == "CLI"
            )
        ]
        tolerated = len(errors) - len(strict_errors)
        if match is None or strict_errors or int(match.group(1), 16) != tolerated:
            print(f"\nScreen ({disc_filename} reported timing failures):")
            for i, row in enumerate(rows):
                print(f"Row {i:2d}: [{row}]")
        assert match is not None, (
            f"{disc_filename}: could not read the failure count off the screen"
        )
        # Each failing instruction prints its own line and increments the count,
        # so with no error lines but the advisory CLI one, the count is that one
        # or zero.
        assert strict_errors == [], (
            f"{disc_filename}: the suite reported mistimed instructions: "
            f"{strict_errors} (see the screen dump)"
        )
        assert int(match.group(1), 16) == tolerated, (
            f"{disc_filename}: suite reported 0x{match.group(1)} timing failures "
            f"but printed {tolerated} advisory CLI line(s) and no others"
        )
        assert failure_zp == tolerated, (
            f"{disc_filename}: &7A = {failure_zp:#04x}, expected {tolerated} (the "
            "suite's own failure tally)"
        )
