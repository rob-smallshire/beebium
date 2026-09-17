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

"""End-to-end fidelity check for issue #70: acheton1984's Tak benchmarks.

acheton1984 ran ten "Tak on 6502" benchmarks on a real BBC B (OS 1.20) with a
6502 Second Processor and on Beebium (github.com/acheton1984/ReTestingTheTak,
MIT), finding Beebium 4.23% fast on average -- the symptom of the unmodelled
DRAM refresh and write-cycle stretch (issue #70). His table (seconds):

    Language     Variant  Original  BBC B + 6502 2nd proc.  Beebium(before)  % fast
    Assembler    TAKAsm       2.64                    2.64             2.53   4.17%
    BASIC        TAK           185                  184.62           176.43   4.44%
    BASIC        TAKfp         248                  247.39           236.44   4.43%
    BASIC        TAKscv       2.80                    2.82             2.70   4.26%
    BASIC        TAKstr        294                  294.25           281.94   4.18%
                                                            Average          4.23%

This runs the short ones on the second processor and checks the elapsed TIME
matches hardware once the board timing is modelled. TAKAsm is pure machine code
(it assembles, runs, and prints `TAK(18,12,6)=7 Time=NNN` in centiseconds).
TAKscv is a BASIC variant, an interpreter-heavy sample under the ordinary BASIC
copied across the Tube; it prints `FNT(18,12,6)=7` and `N.NN SECS.` (seconds).
The long ones (TAK 185 s, TAKfp 248 s, TAKstr 294 s) are skipped. Before the fix
TAKAsm reads about 253; after it, about 262-266 against the hardware's 264.
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

TAK_DISC_FILENAME = "TakBasicAsm.ssd"

# (program, hardware centiseconds). acheton1984's real BBC B + 6502 2nd proc.
CASES = [
    pytest.param("TAKAsm", 264, id="TAKAsm-assembler"),
    pytest.param("TAKscv", 282, id="TAKscv-basic"),
]

TOLERANCE = 0.015  # 1.5% of the hardware time


def _find_tak_disc() -> Path | None:
    repo_root = Path(__file__).parent.parent.parent.parent
    candidate = repo_root / "tests" / "assets" / "discs" / TAK_DISC_FILENAME
    return candidate if candidate.exists() else None


def _step_until_or_timeout(bbc, predicate, emulated_seconds, chunk_seconds=1.0):
    """Advance in fixed cycle chunks until predicate() or the budget expires."""
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


@pytest.fixture
def tak_disc_filepath() -> Path:
    path = _find_tak_disc()
    if path is None:
        pytest.skip(f"Tak benchmark disc not found: {TAK_DISC_FILENAME}")
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
    """A BBC Micro + 6502 second processor + 1770 DFS, booted to the Tube banner."""
    monkeypatch.setenv("BEEBIUM_DISC_WORK_DIR", str(tmp_path / "disc_work"))
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=beebium_server_filepath,
            extra_args=[
                "--tube-65c02",
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
class TestTakBenchmark:
    """acheton1984's Tak benchmarks time the second processor against hardware."""

    @pytest.mark.parametrize("program, hardware_cs", CASES)
    def test_tak_time_matches_hardware(
        self, bbc_tube: Beebium, tak_disc_filepath: Path, program: str, hardware_cs: int
    ) -> None:
        """CHAIN the benchmark and check TAK(18,12,6)=7 and the elapsed TIME.

        Red before #70 was fixed (Beebium ran ~4% fast); green after, within 1.5%
        of acheton1984's real-hardware centiseconds.
        """
        bbc_tube.disc.drive(0).insert(tak_disc_filepath)
        bbc_tube.keyboard.type(f'CHAIN "{program}"\r')

        # The programs report differently: the assembler one prints "Time=NNN"
        # in centiseconds, the BASIC one "N.NN SECS." in seconds.
        def _reported() -> bool:
            return screen_contains(bbc_tube, "Time=") or screen_contains(bbc_tube, "SECS")

        done = _step_until_or_timeout(bbc_tube, _reported, emulated_seconds=45.0, chunk_seconds=1.0)
        rows = read_mode7_screen(bbc_tube)
        if not done:
            print(f"\nScreen ({program} did not print a result):")
            for i, row in enumerate(rows):
                print(f"Row {i:2d}: [{row}]")
            dump_diagnostics(bbc_tube)
        assert done, f"{program} did not print a result within the budget"

        text = "\n".join(rows)
        # TAK(18,12,6) is 7 in every language; a wrong result means a broken run.
        assert "=7" in text.replace(" ", ""), f"expected TAK result 7 in: {rows}"
        m = re.search(r"Time\s*=\s*(\d+)", text)
        if m is not None:
            cs = int(m.group(1))                       # already centiseconds
        else:
            m = re.search(r"(\d+\.\d+)\s*SECS", text)
            assert m is not None, f"could not parse a time from: {rows}"
            cs = round(float(m.group(1)) * 100)        # seconds -> centiseconds

        low = hardware_cs * (1 - TOLERANCE)
        high = hardware_cs * (1 + TOLERANCE)
        print(f"{program}: Time={cs} cs (hardware {hardware_cs}, band [{low:.0f}, {high:.0f}])")
        assert low <= cs <= high, (
            f"{program} ran in {cs} cs, not within {TOLERANCE:.1%} of hardware's "
            f"{hardware_cs} cs (issue #70: second-processor speed)"
        )
