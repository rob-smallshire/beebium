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

"""A stopped machine leaves the server idle: the pacing timer stands still (#119)."""

from __future__ import annotations

import time

from beebium.client import Beebium


def test_pacing_timer_stands_still_while_stopped_and_resumes_on_run(bbc: Beebium) -> None:
    bbc.debugger.ensure_stopped()
    # Let the emulation loop reach its park, which parks the pacing timer.
    time.sleep(0.5)
    stopped_ticks = bbc.system.get_pacing_stats().ticks_executed
    time.sleep(1.0)
    assert bbc.system.get_pacing_stats().ticks_executed == stopped_ticks

    bbc.debugger.ensure_running()
    time.sleep(0.5)
    assert bbc.system.get_pacing_stats().ticks_executed > stopped_ticks


def test_emulation_keeps_real_time_after_a_stop(bbc: Beebium) -> None:
    # The pacing clock is re-anchored when the machine runs again, so it
    # neither races to catch up the stopped interval nor crawls.
    bbc.debugger.ensure_stopped()
    time.sleep(1.0)
    bbc.debugger.ensure_running()
    start_cycles = bbc.debugger.cycle_count
    start = time.monotonic()
    time.sleep(2.0)
    rate = (bbc.debugger.cycle_count - start_cycles) / (time.monotonic() - start)
    assert 1_500_000 < rate < 2_500_000
