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

"""README snippet: set a joystick (uses the pytest `launch_bbc` factory)."""


def test_controllers(launch_bbc):
    bbc = launch_bbc(extra_args=["--joystick"])   # attach the joystick
    bbc.expect("BASIC")
    # readme:begin
    # The controllers attached, each with its named axes and buttons.
    for controller in bbc.controllers.list():
        print(controller.controller_id, [c.id for c in controller.controls])
    # -> joystick ['stick1.x', 'stick1.y', 'fire1', 'stick2.x', 'stick2.y', 'fire2']

    # Push stick 1 hard over and hold its fire button down.
    bbc.controllers.set("joystick", {"stick1.x": 0, "fire1": True})

    bbc.keyboard.type('PRINT "FIRE ";ADVAL(0) AND 1')
    bbc.keyboard.press_return()
    bbc.expect("FIRE 1")
    # readme:end
    assert bbc.controllers.get("joystick")["fire1"] is True
