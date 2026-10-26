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

"""Flashing colours flash: the Video ULA's flash bit picks the colour shown (#226).

A flashing physical colour 8 + c shows c while bit 0 of the control register
(&FE20) is clear and 7 - c while it is set; the MOS toggles the bit in its
vertical sync interrupt at the flash rate. This is the reproduction from the
issue: MODE 1 with logical colour 1 set to flashing red-cyan and the screen
cleared to it, which must be red in frames drawn with the bit clear and cyan in
frames drawn with it set.
"""

from __future__ import annotations

from beebium.client import Beebium
from beebium.client.screen import screen_contains

FLASH_BIT = 0x01
MAX_FIELDS = 150  # three seconds of fields, a few flash periods at the MOS rate


def _red_and_cyan_pixel_counts(frame) -> tuple[int, int]:
    red = cyan = 0
    pixels = frame.pixels
    for i in range(0, frame.width * frame.height * 4, 4):
        blue, green, r = pixels[i], pixels[i + 1], pixels[i + 2]
        if r > 128 and green < 64 and blue < 64:
            red += 1
        elif r < 64 and green > 128 and blue > 128:
            cyan += 1
    return red, cyan


def test_flashing_colour_follows_the_flash_bit(bbc: Beebium) -> None:
    bbc.debugger.stop()
    assert bbc.run_until_or_timeout(lambda: screen_contains(bbc, ">"), emulated_seconds=10.0)
    bbc.keyboard.type("MODE 1\r")
    bbc.keyboard.type("VDU 19,1,9;0;\r")  # logical 1 -> flashing red-cyan
    bbc.keyboard.type("GCOL 0,129:CLG\r")  # clear the screen to logical 1
    bbc.debugger.run()
    bbc.keyboard.wait_until_typing_complete()
    bbc.run_for_emulated_seconds(0.2)  # let the last line run

    # Pair each frame with the flash bit it was drawn under: the MOS changes
    # the bit only in its vertical sync interrupt, so when the register reads
    # the same at the vsync edges either side of a field, the frame completed
    # at the second edge was drawn entirely in that state.
    frames_by_flash: dict[bool, object] = {}
    bbc.run_fields(1)
    for _ in range(MAX_FIELDS):
        before = bbc.video_ula.state.control
        bbc.run_fields(1)
        after = bbc.video_ula.state.control
        if before == after and bool(after & FLASH_BIT) not in frames_by_flash:
            frames_by_flash[bool(after & FLASH_BIT)] = bbc.video.capture_frame()
        if len(frames_by_flash) == 2:
            break

    assert set(frames_by_flash) == {False, True}, "the MOS never toggled the flash bit"

    red, cyan = _red_and_cyan_pixel_counts(frames_by_flash[False])
    assert red > 10 * max(cyan, 1), f"flash bit clear: red={red} cyan={cyan}, expected red"

    red, cyan = _red_and_cyan_pixel_counts(frames_by_flash[True])
    assert cyan > 10 * max(red, 1), f"flash bit set: red={red} cyan={cyan}, expected cyan"
