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

"""Red test for issue #79: a cycle-budget wait must complete while the CPU is halted.

`run_until_or_timeout` implements its emulated-time budget by installing a
full-range address breakpoint with a "cycles >= N" condition. The server
evaluates breakpoints only at an opcode fetch, so while the CPU is halted -- by
holding Break, or after a jam (KIL) opcode -- there are no opcode fetches, the
condition is never evaluated, and the wait runs out its 30-second gRPC deadline
and raises DebuggerError, even though cycle_count keeps advancing.

These assert the DESIRED behaviour: `run_until_or_timeout(lambda: False, 1.0)`
returns False after about one emulated second regardless of what the CPU is
doing. They are RED until #79 is fixed (today they raise at the deadline). This
is the client-level symptom; the mechanism is pinned by the gRPC-level C++ tests
in tests/test_grpc_debugger.cpp.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.screen import screen_contains


@pytest.fixture
def bbc(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
    dfs_1770_rom_filepath: Path,
    tmp_path: pytest.TempPathFactory,
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
                f"slot=14:type=rom:image={dfs_1770_rom_filepath}",
            ],
            startup_timeout=20.0,
        ) as bbc:
            if not bbc.run_until_or_timeout(
                lambda: screen_contains(bbc, "BASIC"), emulated_seconds=15.0
            ):
                pytest.fail("BASIC banner did not appear after boot")
            yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def _assert_budget_completes_in_one_emulated_second(bbc: Beebium) -> None:
    hz = bbc.system.clock_speed_hz or 2_000_000
    start = bbc.debugger.cycle_count
    # DESIRED: returns False after ~1 s emulated. RED today: raises DebuggerError
    # at the 30 s gRPC deadline because the cycle stop never fires while halted.
    result = bbc.run_until_or_timeout(lambda: False, emulated_seconds=1.0)
    delta = bbc.debugger.cycle_count - start
    assert result is False
    assert 0.5 * hz <= delta <= 2.0 * hz, (
        f"expected roughly one emulated second (~{hz} cycles), ran {delta}"
    )


class TestCycleBudgetStopWhileHalted:
    """Issue #79: a cycle-budget wait must complete even when the CPU is halted."""

    def test_completes_while_break_held(self, bbc: Beebium) -> None:
        bbc.debugger.ensure_stopped()
        assert bbc.keyboard.break_down(), "BreakDown RPC failed"
        try:
            _assert_budget_completes_in_one_emulated_second(bbc)
        finally:
            bbc.keyboard.break_up()

    def test_completes_while_jammed(self, bbc: Beebium) -> None:
        # Poison &0900 with a KIL/jam opcode and CALL it, so the CPU halts with
        # no further opcode fetches. Establish the jam with debugger stepping
        # (immune to #79) before the measured window.
        bbc.keyboard.type("?&900=2\r")
        assert bbc.run_until_or_timeout(
            lambda: screen_contains(bbc, ">"), emulated_seconds=3.0
        )
        bbc.keyboard.type("CALL&900\r")
        bbc.debugger.ensure_stopped()
        bbc.debugger.step_cycles(1_000_000)  # let BASIC parse and CALL into the jam

        _assert_budget_completes_in_one_emulated_second(bbc)
