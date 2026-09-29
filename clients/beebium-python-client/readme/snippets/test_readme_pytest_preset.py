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

"""README snippet: configuring the `bbc` fixture with a preset.

The marked region below is rendered verbatim into the README's pytest section,
and is itself a real test run against the installed wheel. Overriding the
fixture in a test module works exactly as it does in a conftest.py.
"""

# readme:begin
# In conftest.py: every `bbc` in scope boots this preset (an id from
# `list-presets`, or a preset file path) -- here a Model B with a disc system.
import pytest


@pytest.fixture(scope="session")
def beebium_preset():
    return "model-b-disc"


def test_has_a_disc_filing_system(bbc):
    bbc.expect("BASIC")
    bbc.keyboard.type("*HELP")
    bbc.keyboard.press_return()
    bbc.expect("DFS")


# readme:end
