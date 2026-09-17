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

"""Scenario guard for issue #71 on the real 6502 second processor.

hoglet's tube_r3_tests program (CHAIN "R3TEST" on tube_r3_tests.ssd) drives the
Tube ULA register 3 FIFO from both sides in four sections -- PH3 and HP3, each
in one-byte and two-byte mode -- and returns to BASIC. A host write to a full R3
register is ignored and completes, so the program runs to the end.

The interesting case is section 2 (HP3 one-byte): the "W W W" pattern writes a
third byte into an already-full R3. On the real ULA that write is dropped and
the host carries on. Beebium once modelled it as a bus stretch, which halted the
host against the waiting coprocessor and deadlocked the pair (issue #71, fixed by
removing the stall). This test guards that regression: it runs hoglet's disc and
checks the program reaches the BASIC prompt and that BASIC still processes input.

The exact register-level transcript (all four sections, line for line) is pinned
by the deterministic C++ golden test tests/test_tube_ula_r3_transcript.cpp; this
scenario test proves the integrated host + coprocessor run the reporter's actual
disc to completion without deadlocking.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.screen import read_mode7_screen, screen_contains

from tube_test_helpers import dump_diagnostics, run_until_or_timeout

_skip_windows_ci = pytest.mark.skipif(
    sys.platform == "win32" and os.environ.get("CI") == "true",
    reason="Tube pacing too timing-sensitive for Windows CI runners",
)

R3_DISC_FILENAME = "tube_r3_tests.ssd"


def _step_until_or_timeout(bbc, predicate, emulated_seconds, chunk_seconds=0.5):
    """Advance the machine in fixed cycle chunks until predicate() or budget.

    Unlike run_until_or_timeout (which stops on a cycle-count breakpoint hit by
    an executing instruction), this steps a fixed number of cycles per chunk, so
    it keeps advancing emulated time even when the host CPU is frozen on a Tube
    bus stretch. That is exactly the #71 deadlock, so this bounds the run and
    returns False on timeout instead of waiting forever for an instruction that
    never executes.
    """
    clock_hz = bbc.system.clock_speed_hz or 2_000_000
    chunk_cycles = max(1, int(chunk_seconds * clock_hz))
    remaining = int(emulated_seconds * clock_hz)
    bbc.debugger.ensure_stopped()
    if predicate():
        return True
    while remaining > 0:
        step = min(chunk_cycles, remaining)
        bbc.debugger.step_cycles(step)
        remaining -= step
        if predicate():
            return True
    return False


def _find_r3_disc() -> Path | None:
    repo_root = Path(__file__).parent.parent.parent.parent
    candidate = repo_root / "tests" / "assets" / "discs" / R3_DISC_FILENAME
    return candidate if candidate.exists() else None


@pytest.fixture
def r3_disc_filepath() -> Path:
    path = _find_r3_disc()
    if path is None:
        pytest.skip(f"Tube R3 test disc not found: {R3_DISC_FILENAME}")
    return path


@pytest.fixture
def bbc_tube(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
    dfs_1770_rom_filepath: Path,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> Beebium:
    """A BBC Micro + 6502 second processor + 1770 DFS, booted to the Tube banner.

    Each test gets its own server process (own PID, and port 0 lets the OS pick a
    free port), and its own disc work directory, so parallel runs on a shared
    machine do not collide.
    """
    monkeypatch.setenv("BEEBIUM_DISC_WORK_DIR", str(tmp_path / "disc_work"))
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=beebium_server_filepath,
            extra_args=[
                "--tube-65c02",
                "--fdc",
                "acorn-1770",
                "--sideways",
                f"14:rom:{dfs_1770_rom_filepath}",
            ],
            startup_timeout=20.0,
        ) as bbc:
            booted = run_until_or_timeout(
                bbc,
                lambda: screen_contains(bbc, "Acorn TUBE"),
                emulated_seconds=30.0,
            )
            if not booted:
                dump_diagnostics(bbc)
                pytest.fail("Tube banner not visible after boot")
            # Left stopped after the boot wait; the test drives it with fixed
            # cycle stepping so the #71 deadlock cannot hang the run.
            yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def _back_at_prompt(bbc) -> bool:
    """True when the program has ended and BASIC is showing its prompt.

    The R3 program fills the screen with status/write/read lines while it runs;
    the bare ">" prompt reappears as the last non-empty row only once it ends
    (the boot prompt has long since scrolled off). This is the program's own
    completion output, polled between step chunks -- not a fixed-time sleep.
    """
    rows = [row.rstrip() for row in read_mode7_screen(bbc)]
    non_empty = [row for row in rows if row]
    return bool(non_empty) and non_empty[-1] == ">"


@_skip_windows_ci
class TestTubeR3TranscriptScenario:
    """Guard the integrated Tube against the issue #71 deadlock, on hoglet's disc."""

    def test_program_runs_to_completion_without_deadlock(
        self, bbc_tube: Beebium, r3_disc_filepath: Path
    ) -> None:
        """The R3 test program runs all four sections and returns to BASIC.

        The third host write of the "W W W" pattern writes into a full R3; the
        ULA drops it and the host carries on. (A regression that re-introduced
        the bus stretch would deadlock the host against the waiting coprocessor
        in the HP3 one-byte section, freezing the screen at "host write data=55".)

        Polls, within a bounded emulated-time budget, for the program's own
        completion output -- the BASIC prompt returning -- so a deadlock times
        out cleanly rather than hanging. Then types a PRINT of a unique token to
        confirm BASIC actually processes input: a live host echoes it, a stalled
        host never would.
        """
        bbc_tube.disc.drive(0).insert(r3_disc_filepath)
        bbc_tube.keyboard.type('CHAIN "R3TEST"\r')

        # Poll for the program to finish and BASIC to return to the prompt.
        finished = _step_until_or_timeout(
            bbc_tube, lambda: _back_at_prompt(bbc_tube),
            emulated_seconds=60.0, chunk_seconds=0.5,
        )
        if not finished:
            rows = read_mode7_screen(bbc_tube)
            print("\nScreen (issue #71 -- a deadlocked build freezes at 'host write data=55'):")
            for i, row in enumerate(rows):
                print(f"Row {i:2d}: [{row}]")
            dump_diagnostics(bbc_tube)
            assert finished, (
                "BASIC did not return to its prompt after CHAIN \"R3TEST\": the host "
                "stalled on a write to a full Tube R3 register (issue #71)."
            )

        # BASIC is back; confirm it still processes input (the host is truly alive).
        bbc_tube.keyboard.type('PRINT "TUBE71DONE"\r')
        completed = _step_until_or_timeout(
            bbc_tube,
            lambda: screen_contains(bbc_tube, "TUBE71DONE"),
            emulated_seconds=10.0,
            chunk_seconds=0.5,
        )

        if not completed:
            rows = read_mode7_screen(bbc_tube)
            print("\nScreen (BASIC did not run the sentinel PRINT):")
            for i, row in enumerate(rows):
                print(f"Row {i:2d}: [{row}]")
            dump_diagnostics(bbc_tube)

        assert completed, (
            "The program reached the BASIC prompt but BASIC did not run the typed "
            "PRINT, so the host is not processing input. See the screen dump."
        )
