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

"""Reusable boot/navigation procedure for the game Firetrack.

Firetrack (Aardvark/Superior, 1987) exercises several corners of the emulator --
custom SAA5050 screen modes among them -- so its boot sequence is factored out
here for reuse across tests. The `bbc_firetrack` fixture (conftest.py) auto-boots
the disc to its instruction story; the helpers below drive it from there to the
loader and on to the custom-SAA5050 intro screens.

The intro is a run of standard MODE 7 story pages, each advanced with SPACE, then
a MODE 4 loader, then three intro screens drawn in a *custom* SAA5050 mode (50
columns by 18 rows rather than the standard 40 by 25). The story pages are waited
for by unique substrings; the timed stages after the loader are waited for by
their on-screen text so the procedure is not tied to exact load durations.
"""

from __future__ import annotations

import time

from beebium.client import Beebium
from beebium.client.exceptions import ScreenExpectTimeout

FIRETRACK_DISC_FILENAME = "DiscA06-FireTrackSAM7.ssd"

# The standard MODE 7 instruction pages, in order, each keyed by a unique
# substring and advanced with SPACE.
STORY_PAGES: tuple[str, ...] = (
    "THE STORY",
    "In principle",
    "Variable defence",
    "ex-military personnel",
    "Devil Rock's",
    "Earth Council",
    "from Shail",
    "complex techniques",
    "gain in confidence",
    "GAME CONTROLS",
    "GAME OPTIONS",
)

# Text on the custom-SAA5050 intro screens, in the order they appear.
LOADER_TEXT = "Now Loading"
INTRO_SCREEN_1_TEXT = "Aardvark"           # ...also "A Software Studios", "Electric  Dreams"
INTRO_SCREEN_2_TEXT = "Computer Screenplay"  # "A Computer Screenplay by Orlando", double height
INTRO_SCREEN_3_TEXT = "White Light"          # "FireTrack", "- Search for the White Light -"


def _advance(bbc: Beebium, page: str, *, timeout: float, key: str = " ") -> None:
    """Wait for `page` then press `key`, confirming the press registered.

    The press is server-paced over the keyboard matrix; if the screen has not
    changed shortly after, the press was dropped (more likely on a slow CI host)
    and is re-driven. Raises ScreenExpectTimeout if `page` never appears.
    """
    bbc.expect(page, timeout=timeout)
    for _ in range(4):
        before = bbc.video.screen_text().text
        bbc.keyboard.type(key)
        settle = time.monotonic() + 4.0
        while time.monotonic() < settle:
            time.sleep(0.3)
            if bbc.video.screen_text().text != before:
                return
    # Screen never changed after several presses -- let the caller's next wait
    # surface it as a real navigation failure rather than looping here.


def navigate_story(bbc: Beebium, *, page_timeout: float = 30.0) -> None:
    """Page through the standard MODE 7 instruction story to the loader."""
    bbc.debugger.ensure_running()
    for page in STORY_PAGES:
        _advance(bbc, page, timeout=page_timeout)


def wait_for_loader(bbc: Beebium, *, timeout: float = 45.0) -> None:
    """Wait for the MODE 4 'Now Loading' screen after the story."""
    bbc.expect(LOADER_TEXT, timeout=timeout)


def wait_for_saa5050_intro(bbc: Beebium, *, timeout: float = 45.0) -> None:
    """Wait for the first custom-SAA5050 intro screen (the Aardvark credits).

    Loads off disc, so it can take tens of seconds after the loader appears.
    """
    bbc.expect(INTRO_SCREEN_1_TEXT, timeout=timeout)


def advance_to_saa5050_intro(bbc: Beebium) -> None:
    """Full path from a freshly booted disc to the custom-SAA5050 intro."""
    navigate_story(bbc)
    wait_for_loader(bbc)
    wait_for_saa5050_intro(bbc)


def is_at_saa5050_intro(bbc: Beebium) -> bool:
    """Best-effort check that an intro screen is currently up."""
    try:
        text = bbc.video.screen_text().text
    except ScreenExpectTimeout:
        return False
    return any(t in text for t in (INTRO_SCREEN_1_TEXT, INTRO_SCREEN_3_TEXT))
