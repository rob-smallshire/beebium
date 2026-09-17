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

"""Scenario reproduction of issue #71 on the real 6502 second processor.

hoglet's tube_r3_tests program (CHAIN "R3TEST" on tube_r3_tests.ssd) drives the
Tube ULA register 3 FIFO from both sides in four sections: PH3 and HP3, each in
one-byte and two-byte mode. On a genuine Ferranti Tube ULA a host write to a
full R3 register is ignored and completes; Beebium instead stalls the host CPU
on a bus stretch (issue #71). Because the coprocessor is waiting for the host to
finish that write, neither side proceeds and the program deadlocks.

The program runs the sections in order:

    1. PH3 one-byte  (Type 0)  -- host reads only; completes
    2. HP3 one-byte  (Type 1)  -- host writes; HANGS on the third write of the
                                  "W W W" pattern (host writes 44, 55, then 66)
    3. PH3 two-byte  (Type 2)  -- never reached today
    4. HP3 two-byte  (Type 3)  -- never reached today

So today the program stops in section 2, exactly as acheton1984 reported, with
the screen showing the HP3 one-byte banner and ending at "host write data=55".
It never prints a "Two Byte Mode" banner.

This test is EXPECTED TO FAIL until #71 is fixed: it CHAINs the program and
waits (with a bounded emulated-time budget, so a stalled run cannot hang CI) for
a "Two Byte Mode" banner that only appears once the machine gets past the HP3
one-byte stall. On failure it prints the screen at the hang, for the record.

The exact register-level transcript (all four sections, line for line) is pinned
by the deterministic C++ golden test tests/test_tube_ula_r3_transcript.cpp; this
scenario test proves the integrated host + coprocessor no longer deadlocks on the
reporter's actual disc.
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


@_skip_windows_ci
class TestTubeR3TranscriptScenario:
    """Reproduce issue #71 with hoglet's actual R3 test disc over the Tube."""

    def test_program_gets_past_the_hp3_one_byte_stall(
        self, bbc_tube: Beebium, r3_disc_filepath: Path
    ) -> None:
        """The R3 test program must run past the HP3 one-byte section.

        RED until #71 is fixed: the third host write of the "W W W" pattern
        stalls the host, the coprocessor never gets its reply, and the program
        deadlocks in section 2, so a "Two Byte Mode" banner (sections 3 and 4)
        never appears. The emulated-time budget bounds the stalled run so the
        test times out cleanly instead of hanging.
        """
        bbc_tube.disc.drive(0).insert(r3_disc_filepath)
        bbc_tube.keyboard.type('CHAIN "R3TEST"\r')

        got_past_hang = _step_until_or_timeout(
            bbc_tube,
            lambda: screen_contains(bbc_tube, "Two Byte Mode"),
            emulated_seconds=60.0,
            chunk_seconds=0.5,
        )

        if not got_past_hang:
            rows = read_mode7_screen(bbc_tube)
            print("\nScreen at the hang (issue #71 -- expect it to end at 'host write data=55'):")
            for i, row in enumerate(rows):
                print(f"Row {i:2d}: [{row}]")
            dump_diagnostics(bbc_tube)

        assert got_past_hang, (
            "Program did not reach a 'Two Byte Mode' section: the host stalled on "
            "a write to a full Tube R3 register (issue #71). See the screen dump above."
        )
