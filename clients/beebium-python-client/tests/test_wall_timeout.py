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

"""Wall-clock deadlines scale with the emulated time a run covers (#123).

A slow host must not time out a long emulated run, and a run that times out
must say whether the host was slow or the machine made no progress.
"""

from __future__ import annotations

import time

import pytest

from beebium.client import Beebium
from beebium.client.debugger import DEFAULT_TIMEOUT, wall_timeout_for
from beebium.client.exceptions import DebuggerError


class TestWallTimeoutFor:
    def test_short_runs_keep_the_default(self) -> None:
        assert wall_timeout_for(0.1) == DEFAULT_TIMEOUT

    def test_a_25_second_chunk_tolerates_a_fifth_of_real_time(self) -> None:
        assert wall_timeout_for(25.0) == 135.0

    def test_unlimited_and_fast_speeds_count_as_real_time(self) -> None:
        assert wall_timeout_for(25.0, 0.0) == wall_timeout_for(25.0, 1.0)
        assert wall_timeout_for(25.0, 4.0) == wall_timeout_for(25.0, 1.0)

    def test_a_slow_configured_speed_stretches_the_deadline(self) -> None:
        assert wall_timeout_for(5.0, 0.2) == 10.0 + 5.0 * 5.0 / 0.2


def test_slow_host_chunk_does_not_time_out(bbc: Beebium) -> None:
    # At a fifth of real time a 7-emulated-second chunk takes about 35 s of
    # wall time: longer than the old fixed 30 s deadline, well inside the
    # scaled one.
    bbc.debugger.ensure_stopped()
    bbc.system.set_speed_multiplier(0.2)
    try:
        start = bbc.debugger.cycle_count
        assert bbc.run_until_or_timeout(lambda: False, 7.0, chunk_seconds=7.0) is False
        assert bbc.debugger.cycle_count - start >= 7 * 2_000_000
    finally:
        bbc.system.set_speed_multiplier(1.0)


def test_stuck_machine_fails_in_bounded_time_saying_so(bbc: Beebium) -> None:
    # Nothing resumes the machine, so the cycle breakpoint can never fire.
    bbc.debugger.ensure_stopped()
    target = bbc.debugger.cycle_count + 2_000_000
    started = time.monotonic()
    with pytest.raises(DebuggerError, match=r"no emulated progress in \d+ s: machine stuck"):
        bbc.debugger.run_to_cycle(
            target, clock_hz=2_000_000, wall_timeout=2.0, resume=lambda: None
        )
    assert time.monotonic() - started < 10.0
    assert len(bbc.debugger.list_breakpoints()) == 0


def test_slow_run_reports_progress_and_throughput(bbc: Beebium) -> None:
    # A run far longer than its deadline, at real time: progress was made.
    bbc.debugger.ensure_stopped()
    target = bbc.debugger.cycle_count + 60 * 2_000_000
    try:
        with pytest.raises(
            DebuggerError,
            match=r"slow host: ran \d+\.\d of 60\.0 emulated seconds in \d+ s wall "
            r"\(throughput \d+\.\d\dx\)",
        ):
            bbc.debugger.run_to_cycle(target, clock_hz=2_000_000, wall_timeout=2.0)
    finally:
        bbc.debugger.ensure_stopped()


def test_explicit_wall_timeout_per_chunk_is_honoured(bbc: Beebium) -> None:
    bbc.debugger.ensure_stopped()
    with pytest.raises(DebuggerError, match="slow host"):
        bbc.run_until_or_timeout(
            lambda: False, 60.0, chunk_seconds=60.0, wall_timeout_per_chunk=2.0
        )
    assert bbc.debugger.is_stopped
