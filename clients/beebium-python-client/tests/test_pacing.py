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

"""Integration tests for the pacing/speed-headroom stats.

These exercise the throughput sampling done by the server's emulation loop,
which a UI speed slider polls via GetPacingStats: the configured multiplier,
the achieved multiplier, and the estimated maximum attainable multiplier.
"""

from __future__ import annotations

import os
import time

import pytest

from beebium.client import Beebium

# The server samples throughput once per pacing window; this is a little over
# one window (~5s wall) so at least one sample is published.
_ONE_WINDOW_SECONDS = 6.5


# The "runs at Nx" tests assert an *absolute* achieved throughput, which only
# holds when the host has the spare CPU to pace a 2 MHz machine at the requested
# multiple of real time. Shared CI runners cannot guarantee that at any given
# moment -- GitHub's macOS runners in particular are very slow and contended, so
# the emulation thread gets only a fraction of wall-clock CPU and the achieved
# rate falls well short. The deterministic pacing/clamp logic is covered by
# tests/test_pacing_controller.cpp; here we skip the throughput-magnitude
# assertions under CI and keep them for local runs on a known machine.
_skip_throughput_in_ci = pytest.mark.skipif(
    os.environ.get("CI", "").lower() in {"true", "1"},
    reason="achieved-throughput pacing assertions need a host with guaranteed "
    "spare CPU; CI runners (notably GitHub macOS) are too slow/contended. The "
    "pacing logic is covered deterministically by test_pacing_controller.cpp.",
)


def test_pacing_stats_reports_configured_multiplier(bbc: Beebium) -> None:
    """get_pacing_stats reports the configured speed multiplier immediately,
    without needing the emulator to run."""
    assert bbc.system.get_pacing_stats().speed_multiplier == 1.0

    bbc.system.set_speed_multiplier(2.0)
    assert bbc.system.get_pacing_stats().speed_multiplier == 2.0


def test_headroom_estimate_populates_after_a_window(bbc: Beebium) -> None:
    """After a pacing window elapses while running, the server publishes a
    throughput sample: an achieved multiplier and an estimated ceiling that is
    never below it."""
    # The fixture's emulator is already running; just let a window elapse.
    assert bbc.debugger.is_running
    time.sleep(_ONE_WINDOW_SECONDS)

    stats = bbc.system.get_pacing_stats()

    # Real-time pacing of a 2 MHz machine: achieved is ~1x and there is ample
    # headroom, so the estimate is at least the achieved rate.
    assert stats.achieved_speed_multiplier > 0.0
    assert stats.estimated_max_speed_multiplier >= stats.achieved_speed_multiplier


# Seconds to run after a speed change before measuring -- several of the
# server's ~1s publish windows, so the achieved rate has settled.
_SETTLE_SECONDS = 3.5


@_skip_throughput_in_ci
def test_runs_at_double_speed(bbc: Beebium) -> None:
    """Setting 2x actually paces the emulator at ~2x real time (a 2 MHz machine
    is far within any modern host's headroom)."""
    assert bbc.debugger.is_running
    bbc.system.set_speed_multiplier(2.0)
    time.sleep(_SETTLE_SECONDS)

    stats = bbc.system.get_pacing_stats()
    assert stats.speed_multiplier == 2.0
    assert 1.6 <= stats.achieved_speed_multiplier <= 2.4


def test_speed_given_at_launch_is_configured(launch_bbc) -> None:
    """--speed at launch is the configured multiplier (#256): it was dropped
    before the server's services existed, leaving 1.0."""
    bbc = launch_bbc(extra_args=["--speed", "2"])
    assert bbc.system.get_pacing_stats().speed_multiplier == 2.0


@_skip_throughput_in_ci
def test_speed_given_at_launch_is_achieved(launch_bbc) -> None:
    """The machine launched with --speed 2 runs at ~2x real time (#256), not
    at the real-time default the pacing clock was reset to."""
    bbc = launch_bbc(extra_args=["--speed", "2"])
    assert bbc.debugger.is_running
    time.sleep(_SETTLE_SECONDS)

    stats = bbc.system.get_pacing_stats()
    assert stats.speed_multiplier == 2.0
    assert 1.6 <= stats.achieved_speed_multiplier <= 2.4


@_skip_throughput_in_ci
def test_runs_at_half_speed(bbc: Beebium) -> None:
    """Setting 0.5x paces the emulator at ~half real time (always attainable)."""
    assert bbc.debugger.is_running
    bbc.system.set_speed_multiplier(0.5)
    time.sleep(_SETTLE_SECONDS)

    stats = bbc.system.get_pacing_stats()
    assert stats.speed_multiplier == 0.5
    assert 0.4 <= stats.achieved_speed_multiplier <= 0.6


@_skip_throughput_in_ci
def test_runs_above_the_catch_up_clamp(bbc: Beebium) -> None:
    """Paced speed must be able to exceed the controller's real-time catch-up
    ceiling (~3x): setting 4x reaches ~4x, not a ~3x plateau. Needs only modest
    headroom -- a 2 MHz machine at 4x is trivial for any modern host."""
    assert bbc.debugger.is_running
    bbc.system.set_speed_multiplier(4.0)
    time.sleep(_SETTLE_SECONDS)

    stats = bbc.system.get_pacing_stats()
    assert stats.speed_multiplier == 4.0
    assert stats.achieved_speed_multiplier >= 3.5
