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

"""README snippet: read the screen text (uses the pytest `bbc` fixture)."""


def test_screen(bbc):
    bbc.expect("BASIC")
    # readme:begin
    # Read the text the machine is displaying, whatever the screen mode.
    text = bbc.video.screen_text().text

    # Wait (up to a timeout) for text to appear -- pexpect-style automation.
    bbc.expect("BASIC")
    # readme:end
    assert "BASIC" in text
