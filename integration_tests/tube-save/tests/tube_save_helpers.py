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

"""Shared constants and helpers for the Tube DFS SAVE tests.

A uniquely named module (not conftest.py) for symbols the test module and the
conftest fixtures share. Importing a conftest.py by its bare name is fragile --
when several conftest.py files are collected together pytest can bind the name
`conftest` to the wrong one -- so shared symbols live here instead.
"""

from __future__ import annotations

from beebium.client import Beebium

# Tube OSRDCH is slower than native -- use a longer per-key delay.
TUBE_CYCLES_PER_KEY = 200_000

# DFS SSD geometry: 40 tracks * 10 sectors * 256 bytes.
SSD_SIZE = 102400


def run_until_or_timeout(bbc: Beebium, predicate, emulated_seconds: float,
                         chunk_seconds: float = 1.0) -> bool:
    """Run until predicate is met or timeout."""
    return bbc.run_until_or_timeout(
        predicate, emulated_seconds, chunk_seconds=chunk_seconds)
