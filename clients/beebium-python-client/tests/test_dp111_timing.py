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
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> Beebium:
    """A stock Model B + 1770 DFS booted to BASIC, isolated per test."""
    monkeypatch.setenv("BEEBIUM_DISC_WORK_DIR", str(Path(tmp_path) / "disc_work"))
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=beebium_server_filepath,
            extra_args=[
                "--fdc",
                "acorn-1770",
                "--sideways",
                f"14:rom:{dfs_1770_rom_filepath}",
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
        if match is None or match.group(1).upper() != "00" or failure_zp != 0:
            print(f"\nScreen ({disc_filename} reported timing failures):")
            for i, row in enumerate(rows):
                print(f"Row {i:2d}: [{row}]")
        assert match is not None, (
            f"{disc_filename}: could not read the failure count off the screen"
        )
        # The screen tally is authoritative (each failing instruction prints its
        # own line and increments this count); zero means no error lines at all.
        assert match.group(1).upper() == "00", (
            f"{disc_filename}: suite reported 0x{match.group(1)} timing failures "
            "(each is a mistimed 6502 instruction; see the screen dump)"
        )
        assert failure_zp == 0, (
            f"{disc_filename}: &7A = {failure_zp:#04x}, expected 0 (the suite's "
            "own failure tally)"
        )
