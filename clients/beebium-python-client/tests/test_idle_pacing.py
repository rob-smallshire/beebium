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

import pytest

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


def _achieved_rate(bbc: Beebium, seconds: float) -> float:
    """Cycles per wall second while the machine runs for `seconds`."""
    start_cycles = bbc.debugger.cycle_count
    start = time.monotonic()
    time.sleep(seconds)
    return (bbc.debugger.cycle_count - start_cycles) / (time.monotonic() - start)


# 0.3x stands in for a heavily loaded host: the property is relative, so it
# must hold however fast the host manages to run the machine.
@pytest.mark.parametrize("speed", [1.0, 0.3])
def test_pacing_after_a_stop_matches_the_rate_before_it(bbc: Beebium, speed: float) -> None:
    # The pacing clock is re-anchored when the machine runs again, so after a
    # stop it neither races to catch up the stopped interval (a burst) nor
    # crawls. Compare the rate after the resume with the rate before the stop
    # on the same host, rather than with wall-clock real time, which a shared
    # CI runner cannot guarantee (#123).
    bbc.system.set_speed_multiplier(speed)
    bbc.debugger.ensure_running()
    time.sleep(0.5)  # let pacing settle at the new speed
    before = _achieved_rate(bbc, 2.0)

    bbc.debugger.ensure_stopped()
    time.sleep(2.0)  # a missed re-anchor would try to catch this up
    bbc.debugger.ensure_running()
    after = _achieved_rate(bbc, 2.0)

    print(f"\nspeed x{speed:g}: {before:,.0f} cycles/s before the stop, "
          f"{after:,.0f} cycles/s after the resume")
    assert before > 200_000, (
        f"slow host: the machine ran at {before:,.0f} cycles/s "
        f"(throughput {before / 2_000_000:.2f}x) before the stop")
    ratio = after / before
    assert ratio < 1.3, f"catch-up burst after the resume: {ratio:.2f}x the rate before the stop"
    assert ratio > 0.7, f"crawl after the resume: {ratio:.2f}x the rate before the stop"
