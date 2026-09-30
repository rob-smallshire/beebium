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

"""Pytest plugin that renices every server a test launches to +20.

Launched servers are child processes of the pytest process. Before each test
runs, this lowers the priority of every child to +20, so the server yields the
CPU to any other load on the host -- noisy_load.py, say -- the way it does on
an oversubscribed CI runner. Only direct children of this pytest process, by
PID, are touched.

Enable it with ``-p``, from clients/beebium-python-client::

    PYTHONPATH=../../tools/slow-host \\
        uv run --group test python -m pytest tests/test_idle_pacing.py -p renice_servers_plugin
"""

from __future__ import annotations

import os
import subprocess

import pytest


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_call(item):
    children = subprocess.run(["pgrep", "-P", str(os.getpid())],
                              capture_output=True, text=True).stdout.split()
    for pid in children:
        subprocess.run(["renice", "+20", "-p", pid], stdout=subprocess.DEVNULL)
    yield
