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

"""Beebium.launch(extra_env=...) sets variables for the server it starts, and for no one else.

The tests give BEEBIUM_NO_PACING, which a server reads at startup: with it, the pacing loop
does not run, so the server's pacing statistics count no ticks however long the machine runs.
"""

from __future__ import annotations

import os
import time

from beebium.client import Beebium


def _paced(bbc: Beebium) -> bool:
    time.sleep(0.5)
    return bbc.system.get_pacing_stats().ticks_executed > 0


def test_extra_env_reaches_the_launched_server(server_installation, monkeypatch):
    monkeypatch.delenv("BEEBIUM_NO_PACING", raising=False)
    with Beebium.launch(server=server_installation, extra_env={"BEEBIUM_NO_PACING": "1"}) as bbc:
        assert not _paced(bbc)


def test_extra_env_leaves_this_process_environment_alone(server_installation, monkeypatch):
    monkeypatch.delenv("BEEBIUM_NO_PACING", raising=False)
    with Beebium.launch(server=server_installation, extra_env={"BEEBIUM_NO_PACING": "1"}):
        assert "BEEBIUM_NO_PACING" not in os.environ
    with Beebium.launch(server=server_installation) as bbc:
        assert _paced(bbc)
