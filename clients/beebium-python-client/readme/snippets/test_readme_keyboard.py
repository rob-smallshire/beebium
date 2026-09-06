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

"""README snippet: type on the keyboard (uses the pytest `bbc` fixture)."""


def test_keyboard(bbc):
    bbc.expect("BASIC")
    # readme:begin
    bbc.keyboard.type("PRINT 2+2")     # type text, shifting as needed
    bbc.keyboard.press_return()

    bbc.keyboard.key_down("A")         # or drive individual keys
    bbc.keyboard.key_up("A")
    # readme:end
