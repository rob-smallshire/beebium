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


# How long the machine stays stopped. Un-anchored pacing would owe the whole
# interval afterwards: STOP_SECONDS x the rate in cycles of deficit.
STOP_SECONDS = 3.0
# How long after the resume the pacing accounting is watched.
WATCH_SECONDS = 2.0
# Slack for the pacing controller's tick granularity, in seconds of emulated
# time at the configured rate: it runs up to a few ticks ahead of its target
# while on time, and a sample can land just after a tick.
TICK_SLACK_SECONDS = 0.05


# 0.3x stands in for a slow host; the heavy-load behaviour this test was
# written against (#137) was reproduced with the server reniced below a CPU
# load that came and went.
@pytest.mark.parametrize("speed", [1.0, 0.3])
def test_pacing_is_re_anchored_after_a_stop(bbc: Beebium, speed: float) -> None:
    """After a stop the pacing neither bursts to catch up nor crawls (#119).

    The property is checked against the server's own pacing accounting, not
    against wall-clock throughput: on a loaded host the rate over any short
    window swings both ways with the load (0.55x to 1.95x has been measured
    across a stop with nothing wrong), because a machine that falls behind
    catches up afterwards, as pacing should. What a pacing fault changes is
    the accounting itself:

    - a burst carries the stopped interval's owed time past the resume, so the
      controller's deficit (target minus actual cycles, positive when behind)
      starts out near STOP_SECONDS of cycles instead of near zero;
    - a crawl comes from the controller believing the machine is ahead of its
      target, a strongly negative deficit, or from the pacing timer not
      running again, so its tick count stands still;
    - neither pacing fault can make the machine run more cycles than real time
      allows since the resume.
    """
    rate = 2_000_000 * speed
    slack_cycles = TICK_SLACK_SECONDS * rate
    bbc.system.set_speed_multiplier(speed)
    bbc.debugger.ensure_running()
    before = _achieved_rate(bbc, 2.0)

    bbc.debugger.ensure_stopped()
    time.sleep(STOP_SECONDS)
    stopped = bbc.system.get_pacing_stats()
    stopped_cycles = bbc.debugger.cycle_count

    resumed_at = time.monotonic()
    bbc.debugger.ensure_running()
    first_drift = None
    drifts = []
    last_ticks = stopped.ticks_executed
    while time.monotonic() - resumed_at < WATCH_SECONDS:
        stats = bbc.system.get_pacing_stats()
        # Samples from before the pacing timer ticked again still show the
        # accounting from before the stop.
        if stats.ticks_executed > stopped.ticks_executed:
            if first_drift is None:
                first_drift = stats.controller_drift
            drifts.append(stats.controller_drift)
        last_ticks = stats.ticks_executed
        time.sleep(0.02)
    cycles_run = bbc.debugger.cycle_count - stopped_cycles
    elapsed = time.monotonic() - resumed_at
    after = _achieved_rate(bbc, 2.0)

    # Wall-clock rates are only diagnostics: on a loaded host they swing.
    print(f"\nspeed x{speed:g}: {before:,.0f} cycles/s before the stop, {after:,.0f} after; "
          f"deficit before the stop {stopped.controller_drift:,.0f}, first after the resume "
          f"{first_drift}, range {min(drifts, default=0):,.0f} to {max(drifts, default=0):,.0f}")

    assert last_ticks - stopped.ticks_executed > 100, (
        "crawl: the pacing timer did not run again after the resume")
    assert first_drift is not None
    assert first_drift < 0.25 * STOP_SECONDS * rate, (
        f"burst: the pacing deficit right after the resume was {first_drift:,.0f} cycles, "
        f"close to the {STOP_SECONDS * rate:,.0f} owed for the stop; it was not re-anchored")
    assert min(drifts) > -slack_cycles, (
        f"crawl: the pacing controller believed the machine {-min(drifts):,.0f} cycles ahead "
        f"of its target after the resume, and throttled it")
    assert cycles_run <= rate * elapsed + slack_cycles, (
        f"burst: {cycles_run:,} cycles ran in the {elapsed:.2f} s after the resume, more than "
        f"real time at x{speed:g} allows ({rate * elapsed:,.0f})")
