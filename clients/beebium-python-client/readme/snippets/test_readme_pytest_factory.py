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

"""README snippet: the pytest `launch_bbc` factory fixture.

The marked region below is rendered verbatim into the README's pytest section,
and is itself a real test run against the installed wheel.
"""


# readme:begin
# launch_bbc launches machines on demand -- any variant, any preset, as many
# as a test needs -- and tears every one of them down when the test ends.
def test_same_program_on_two_machines(launch_bbc):
    model_b = launch_bbc(preset="model-b-disc")
    b_plus = launch_bbc(variant="model-b-plus")
    for bbc in (model_b, b_plus):
        bbc.expect("BASIC")
        bbc.keyboard.type("PRINT 6*7")
        bbc.keyboard.press_return()
        assert bbc.expect("42") == "42"


# readme:end
