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

"""Scenario reproduction of issue #70: the second processor runs ~4% fast.

acheton1984 measured ten "Tak on 6502" benchmarks 4.23% faster on Beebium than
on a real 6502 Second Processor (#70). tube-architect's design note
docs/discussion/tube-coprocessor-board-timing.md attributes the gap to two
board effects the emulator does not model: a DRAM refresh cycle stolen roughly
one in 44, and a one-crystal-period stretch on every write cycle. tom_seddon
measured the wedge at 2.922 MHz for an LDA-zp loop and 2.703 MHz for an STA-zp
loop, and the internal 65C102 at 3.939 MHz for both
(https://stardot.org.uk/forums/viewtopic.php?t=25167).

This reproduces tom_seddon's measurement on the coprocessor. tube_speed70.ssd
carries R70 (generated from tube_speed70.bas): it assembles two loops of
identical structure and cycle count -- one all LDA zp (every cycle a read), one
all STA zp (one write cycle in three) -- times each with the host TIME, and
prints the effective MHz. On a real 6502 Second Processor R70 would print about
2.93 (LDA) and 2.70 (STA); on the 65C102 about 3.94 for both. Beebium today runs
every coprocessor cycle at the exact 3.000 / 4.000 MHz ratio, so it prints 3.00
/ 4.00 for both -- so both assertions are red until #70 is fixed.

EXPECTED TO FAIL until #70 is fixed (test-first red); no fix is included here.
"""

from __future__ import annotations

import os
import re
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

SPEED_DISC_FILENAME = "tube_speed70.ssd"

# (coprocessor CLI flag, expected LDA MHz, expected STA MHz). The wedge stretches
# writes (STA slower than LDA); the 65C102 board does not (both equal).
CASES = [
    pytest.param("tube-65c02", 2.93, 2.70, id="6502-second-processor-3MHz"),
    pytest.param("tube-65c102", 3.94, 3.94, id="65C102-coprocessor-4MHz"),
]

TOLERANCE = 0.005  # 0.5%, per the design note's acceptance figures


def _find_speed_disc() -> Path | None:
    repo_root = Path(__file__).parent.parent.parent.parent
    candidate = repo_root / "tests" / "assets" / "discs" / SPEED_DISC_FILENAME
    return candidate if candidate.exists() else None


def _step_until_or_timeout(bbc, predicate, emulated_seconds, chunk_seconds=1.0):
    """Advance the machine in fixed cycle chunks until predicate() or budget.

    Fixed-cycle stepping keeps advancing emulated time regardless of what the
    guest is doing, so a hang cannot wedge the run; it just times out.
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


def _parse_mhz(rows: list[str], label: str) -> float | None:
    text = "\n".join(rows)
    m = re.search(label + r"=\s*([0-9]+(?:\.[0-9]+)?)", text)
    return float(m.group(1)) if m else None


@pytest.fixture
def speed_disc_filepath() -> Path:
    path = _find_speed_disc()
    if path is None:
        pytest.skip(f"Speed test disc not found: {SPEED_DISC_FILENAME}")
    return path


@pytest.fixture
def coprocessor_flag(request) -> str:
    return request.param


@pytest.fixture
def bbc_copro(
    coprocessor_flag: str,
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
    dfs_1770_rom_filepath: Path,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> Beebium:
    """A BBC Micro with the named coprocessor and 1770 DFS, booted to the banner.

    Own server process (port 0 picks a free port) and own disc work directory,
    so parallel runs on a shared machine do not collide.
    """
    monkeypatch.setenv("BEEBIUM_DISC_WORK_DIR", str(tmp_path / "disc_work"))
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=beebium_server_filepath,
            extra_args=[
                f"--{coprocessor_flag}",
                "--fdc", "acorn-1770",
                "--sideways", f"14:rom:{dfs_1770_rom_filepath}",
            ],
            startup_timeout=20.0,
        ) as bbc:
            booted = run_until_or_timeout(
                bbc, lambda: screen_contains(bbc, "Acorn TUBE"), emulated_seconds=30.0
            )
            if not booted:
                dump_diagnostics(bbc)
                pytest.fail("Tube banner not visible after boot")
            yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))


@_skip_windows_ci
class TestTubeSpeed:
    """Reproduce issue #70: the coprocessor's effective clock is too fast."""

    @pytest.mark.parametrize("coprocessor_flag, expected_lda, expected_sta", CASES, indirect=["coprocessor_flag"])
    def test_effective_mhz_matches_hardware(
        self, bbc_copro: Beebium, speed_disc_filepath: Path,
        coprocessor_flag: str, expected_lda: float, expected_sta: float,
    ) -> None:
        """The LDA and STA loops must read the hardware's effective MHz.

        Red until #70 is fixed: Beebium runs both loops at the exact clock ratio
        (3.00 / 4.00 MHz), where the real boards run slower -- and, on the wedge,
        the store loop slower still than the load loop.
        """
        bbc_copro.disc.drive(0).insert(speed_disc_filepath)
        bbc_copro.keyboard.type('CHAIN "R70"\r')

        finished = _step_until_or_timeout(
            bbc_copro, lambda: screen_contains(bbc_copro, "DONE70"),
            emulated_seconds=60.0, chunk_seconds=1.0,
        )
        rows = read_mode7_screen(bbc_copro)
        if not finished:
            print("\nScreen (R70 did not finish):")
            for i, row in enumerate(rows):
                print(f"Row {i:2d}: [{row}]")
            dump_diagnostics(bbc_copro)
        assert finished, "R70 did not print DONE70 within the budget"

        lda = _parse_mhz(rows, "LDAMHZ")
        sta = _parse_mhz(rows, "STAMHZ")
        print(f"\n{coprocessor_flag}: LDAMHZ={lda} (expect ~{expected_lda}) "
              f"STAMHZ={sta} (expect ~{expected_sta})")
        assert lda is not None and sta is not None, f"could not parse MHz from screen: {rows}"

        # Both are red today at ~3.00 / ~4.00.
        assert abs(lda - expected_lda) <= expected_lda * TOLERANCE, (
            f"LDA loop effective clock {lda} MHz is not within {TOLERANCE:.1%} of "
            f"{expected_lda} MHz (issue #70: board timing not modelled)"
        )
        assert abs(sta - expected_sta) <= expected_sta * TOLERANCE, (
            f"STA loop effective clock {sta} MHz is not within {TOLERANCE:.1%} of "
            f"{expected_sta} MHz (issue #70: refresh + write stretch not modelled)"
        )
