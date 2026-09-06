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

"""README snippet: the pytest `bbc` fixture, shown as a whole test file.

The marked region below is rendered verbatim into the README's pytest example,
and is itself a real test run against the installed wheel.
"""
# readme:begin
# The beebium pytest plugin registers a `bbc` fixture: a fresh BBC Micro,
# launched and torn down per test. Install beebium and it is available with no
# conftest wiring.
def test_print(bbc):
    bbc.expect("BASIC")
    bbc.keyboard.type("PRINT 2+2")
    bbc.keyboard.press_return()
    assert bbc.expect("4") == "4"
# readme:end
