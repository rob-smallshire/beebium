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

"""video.screen_region() says where the displayed screen lies in memory (#195).

The server computes it from the live 6845, System VIA latch and paging, so the
bytes it names are the ones on display: checked against the teletext grid the
video drew, which comes from the same fetches.
"""

from __future__ import annotations

import pytest

from beebium.client import Beebium


def _boot(launch_bbc, variant: str) -> Beebium:
    bbc = launch_bbc(variant=variant)
    bbc.system.set_speed_multiplier(0.0)
    bbc.debugger.ensure_running()
    bbc.wait_until_ready()
    return bbc


def _command(bbc: Beebium, text: str) -> None:
    """Type a command and let it finish, leaving the machine stopped."""
    bbc.debugger.ensure_running()
    bbc.keyboard.type(text + "\r")
    bbc.keyboard.wait_until_typing_complete()
    bbc.run_until_or_timeout(lambda: False, 1.0, chunk_seconds=1.0)


def _screen_bytes(bbc: Beebium) -> bytes:
    r = bbc.video.screen_region()
    region = bbc.memory.region(r.region)
    return b"".join(bytes(region.peek[start:end]) for start, end in r.ranges())


def _shows_displayed_text(bbc: Beebium) -> None:
    """The region's bytes, in display order, are the teletext the video drew."""
    screen = bbc.video.teletext_screen()
    assert screen.active
    drawn = bytes(cell.character for cell in screen.cells)
    assert bytes(b & 0x7F for b in _screen_bytes(bbc)) == drawn


def test_model_b_mode_7(launch_bbc) -> None:
    bbc = _boot(launch_bbc, "model-b")
    _command(bbc, 'PRINT "SCREEN"+" REGION"')
    r = bbc.video.screen_region()
    assert (r.region, r.teletext, r.length, r.wrap_address) == ("main_ram", True, 1000, 0x7C00)
    assert 0x7C00 <= r.start_address < 0x8000
    _shows_displayed_text(bbc)


def test_model_b_mode_2_follows_the_hardware_scroll(launch_bbc) -> None:
    bbc = _boot(launch_bbc, "model-b")
    _command(bbc, "MODE 2")
    r = bbc.video.screen_region()
    assert (r.region, r.teletext) == ("main_ram", False)
    assert (r.start_address, r.length, r.wrap_address) == (0x3000, 80 * 32 * 8, 0x3000)
    assert r.ranges() == [(0x3000, 0x8000)]

    _command(bbc, "FOR I%=1 TO 40:PRINT I%:NEXT")
    scrolled = bbc.video.screen_region()
    assert scrolled.start_address != 0x3000  # moved by the hardware scroll
    assert scrolled.wrap_address == 0x3000
    (first, second) = scrolled.ranges()
    assert first == (scrolled.start_address, 0x8000)
    assert second == (0x3000, scrolled.start_address)


@pytest.mark.parametrize(("mode", "region"), [(7, "main_ram"), (135, "shadow_ram")])
def test_b_plus_shows_shadow_ram_only_in_a_shadow_mode(launch_bbc, mode: int, region: str) -> None:
    bbc = _boot(launch_bbc, "model-b-plus")
    _command(bbc, f'MODE {mode}:PRINT "SCREEN"+" REGION"')
    assert bbc.video.screen_region().region == region
    _shows_displayed_text(bbc)


def test_integra_b_video_reads_main_ram_in_a_shadow_mode(launch_bbc) -> None:
    bbc = _boot(launch_bbc, "model-b-integra-b")
    # Looping in BASIC, so it stops there: in the OS's idle loop IBOS selects
    # main memory for the CPU (MEMSEL), and shadow RAM would not be active.
    _command(bbc, 'MODE 135:PRINT "SCREEN"+" REGION":REPEAT UNTIL FALSE')
    regions = {r.name: r for r in bbc.memory.regions}
    assert regions["shadow_ram"].active  # the CPU sees shadow RAM...
    assert bbc.video.screen_region().region == "main_ram"  # ...the video does not
    _shows_displayed_text(bbc)
