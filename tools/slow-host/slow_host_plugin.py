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

"""Pytest plugin that makes this host behave like a slow CI runner.

Every request for real-time speed (``System.set_speed_multiplier(1.0)``) is
served at SLOW_HOST_SPEED instead (default 0.3); other speeds, including 0.0
(unlimited), pass through unchanged. Tests that fast-forward unpaced and then
return to 1x -- the usual pattern -- therefore do their real-time parts at the
slow speed, as they would on a loaded runner, without editing the tests. It is
how the slow-host failures of #123, #125 and #132 are reproduced locally.

Enable it with ``-p`` and put this directory on the path, from
clients/beebium-python-client::

    SLOW_HOST_SPEED=0.4 PYTHONPATH=../../tools/slow-host \\
        uv run --group test python -m pytest tests/test_autoboot.py -p slow_host_plugin -s

At the end of the session it prints how many 1x requests it served slowly, so
a run that never asked for 1x (and so was not slowed) is visible.
"""

from __future__ import annotations

import os

from beebium.client.system import System

SPEED = float(os.environ.get("SLOW_HOST_SPEED", "0.3"))

_original_set_speed_multiplier = System.set_speed_multiplier
_served: list[float] = []


def _slow_set_speed_multiplier(self: System, speed_multiplier: float) -> float:
    actual = SPEED if speed_multiplier == 1.0 else speed_multiplier
    _served.append(actual)
    return _original_set_speed_multiplier(self, actual)


def pytest_configure(config) -> None:
    System.set_speed_multiplier = _slow_set_speed_multiplier


def pytest_unconfigure(config) -> None:
    System.set_speed_multiplier = _original_set_speed_multiplier
    print(f"\nslow-host plugin: {_served.count(SPEED)} speed requests of 1x served as {SPEED}x")
