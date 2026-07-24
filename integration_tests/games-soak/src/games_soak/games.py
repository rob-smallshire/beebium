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

"""The game table: how to boot each game and reach playable/attract state.

Each recipe is copied from the corresponding python-client integration test,
which is where the boot sequence was originally worked out and is kept green:

    Revs         -> tests/test_revs_timing.py
    Chuckie Egg  -> tests/test_tube_chuckie_egg.py
    Elite        -> tests/test_tube_elite.py

Keep them in step with those tests; if a boot sequence changes there, mirror it
here. New games are added by working out a boot recipe (booting by hand,
reading landmarks off the screen with the screen helpers) and appending a Game
record. Landmark and navigation text is waited for in real time via the
screen-text expect mechanism (Beebium.expect), which samples the running screen
in any screen mode and never touches the debugger's execution-state stream. Most
games show a title/menu to wait on; a few (e.g. Meteors) come up straight in a
graphics mode and page through a "press SPACE" prompt instead.

All games share the same Model B base (MOS 1.20, BASIC 2, Acorn 1770 DFS 2.26 in
slot 14); Tube games add --tube-65c02, so a run covers one group (--tube or not).
Games auto-boot via Beebium.boot_disc (a Shift-Break), so no per-game boot
command is needed. Revs, Chuckie Egg and Elite recipes come from the matching
tests; Galaforce and Galaforce 2 were worked out by hand.
"""

from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True)
class Game:
    """A single game and the steps to bring it up to a running state."""

    name: str

    # --- machine configuration ---
    server_binary: str = "beebium-model-b"
    mos_rom: str = "acorn-mos_1_20.rom"       # filename under roms/
    basic_rom: str | None = "bbc-basic_2.rom"  # filename under roms/, or None
    fdc: str | None = "acorn-1770"
    sideways: tuple[str, ...] = ("14:rom:acorn-dfs_2_26.rom",)  # "SLOT:rom:FILE"
    tube_args: tuple[str, ...] = ()            # e.g. ("--tube-65c02",)

    # --- disc + boot ---
    disc: str = ""              # path relative to the repo root
    # Games auto-boot via Beebium.boot_disc (Shift-Break), so no per-game boot
    # command is needed. For Tube games, wait for this banner (the second
    # processor coming up) before auto-booting.
    boot_banner: str | None = None

    # --- navigation to a running/attract state ---
    # Confirms the game booted before navigating; None skips the check.
    landmark: str | None = None
    # (wait_text, key) pairs: wait until wait_text appears, then type key.
    nav: tuple[tuple[str, str], ...] = ()
    # Real-time budget for the landmark and each nav step to appear on screen
    # (waited for via the screen-text expect mechanism).
    landmark_timeout_seconds: float = 60.0

    # Real-time keypresses to drive an attract/demo mode, pressed AFTER nav with
    # the machine running. Each press waits (sampling the screen) for the screen
    # to change before the next, so no press is sent before the game has
    # responded -- robust to the seconds a game takes to load before it accepts
    # the first keypress. Used for games (e.g. Galaforce) that page through
    # instruction screens rather than stopping at one distinct landmark.
    attract_keys: tuple[str, ...] = ()
    # Max wall-clock seconds to wait for the screen to change after each press
    # (covers slow page transitions and built-in pauses, e.g. Cylon Attack).
    attract_change_timeout: float = 8.0
    # Real-time settle before the first attract keypress, to let a game whose
    # title has no Mode 7 landmark finish loading before we start pressing keys.
    attract_delay_seconds: float = 0.0

    # pexpect-style paging: while page_prompt is on screen, press page_key and
    # wait for it to advance, until the prompt is gone. Robust to variable page
    # counts and load timing (each press waits for the prompt), so it is
    # preferred over a fixed attract_keys count when a game shows a consistent
    # "press X to continue" prompt on each page.
    page_prompt: str | None = None
    page_key: str = " "

    # How long to let the game run (in its attract/demo mode or gameplay) in
    # real time while watching for a freeze, before moving to the next game.
    run_minutes: float = 5.0


# Revs navigation: verbatim from tests/test_revs_timing.py REVS_BOOT_SEQUENCE.
# Each (wait_text, key): wait for wait_text on the Mode 7 screen, then type key.
_REVS_NAV: tuple[tuple[str, str], ...] = (
    ("Keys:", " "),
    ("Select keyboard", " "),
    ("A Formula 3 car", " "),
    ("The pedals", " "),
    ("In your Ralt", " "),
    ("speedometer", " "),
    ("Loading the game", " "),
    ("See how revs are lost", " "),
    ("Having elected to enter", " "),
    ("Having decided on how long", " "),
    ("Racing involves more", " "),
    ("Along the top", " "),
    ("The precise", " "),
    ("PRACTICE", "1"),
    ("PRESS SPACE BAR TO CONTINUE", " "),
    ("SELECT WING SETTINGS", "\r"),
    ("front", "\r"),
    ("PRESS SPACE BAR TO CONTINUE", " "),
)


GAMES: list[Game] = [
    Game(
        name="Revs",
        disc="discs/games/Disc015-Revs.ssd",
        # The boot sequence itself is the landmark walk; no separate banner.
        nav=_REVS_NAV,
        landmark_timeout_seconds=60.0,
    ),
    Game(
        name="Chuckie Egg 2023",
        tube_args=("--tube-65c02",),
        disc="tests/assets/discs/chuckieEgg2023.ssd",
        boot_banner="Acorn TUBE",
        landmark="A game of skill",
        # The title screen waits for a keypress; pressing space starts play.
        nav=(("A game of skill", " "),),
        # The game takes a long time to load through the Tube.
        landmark_timeout_seconds=300.0,
    ),
    Game(
        name="Elite",
        tube_args=("--tube-65c02",),
        disc="tests/assets/discs/Disc999-EliteSNG45.ssd",
        boot_banner="Acorn TUBE",
        landmark="6502 Second Processor ELITE",
        # The !BOOT loader shows the banner then switches to a graphics mode;
        # confirming the banner and letting it run is enough for the soak. Elite
        # enters an attract mode on its own, so it exercises the machine.
        nav=(),
        landmark_timeout_seconds=60.0,
    ),
    Game(
        name="Galaforce",
        disc="discs/games/Disc025-Galaforce.ssd",
        # Mode 7 instructions screen; unique opening line of the story text.
        landmark="In the midst of the",
        # Press SPACE seven times at ~1s intervals to advance through the
        # instruction pages to the hi-score table and self-play attract mode.
        attract_keys=(" ", " ", " ", " ", " ", " ", " "),
    ),
    Game(
        name="Galaforce 2",
        disc="discs/games/Disc039-Galaforce2PIAS6.ssd",
        # Mode 7 instructions screen; unique opening line of the story text.
        landmark="Everything that Galaforce was",
        # Press SPACE four times at ~1s intervals to reach hi-scores/attract.
        attract_keys=(" ", " ", " ", " "),
    ),
    Game(
        name="Meteors",
        disc="discs/games/Disc001-Meteors.ssd",
        # Instruction pages each show "Press SPACE BAR to continue"; page through
        # them until the prompt is gone (reaches the game/attract). Robust to the
        # graphics title loading before the prompt appears -- a blind count lost
        # the first press and stuck on the last page.
        page_prompt="Press SPACE BAR to continue",
    ),
    Game(
        name="Zalaga",
        disc="discs/games/Disc003-Zalaga.ssd",
        landmark="Zalaga",  # Mode 7 title ("Aardvark Software presents... Zalaga")
        # Return x5 at ~1s intervals, then K, to reach the game.
        attract_keys=("\r", "\r", "\r", "\r", "\r", "K"),
    ),
    Game(
        name="Battlezone",
        disc="discs/games/Disc173-BATTLEZONERSTD.ssd",
        landmark="BATTLEZONE",  # Mode 7 title ("ROCKETEER presents BATTLEZONE")
        # SPACE x7 at ~1s intervals to reach attract mode.
        attract_keys=(" ", " ", " ", " ", " ", " ", " "),
    ),
    Game(
        name="Cylon Attack",
        disc="discs/games/Disc001-CylonAttackAFSTD.ssd",
        # The "A 3D Space battle" title takes a long time to appear; the game
        # then asks "Load high score table (Y/N)?" -- answer N (waiting for that
        # exact prompt) to drop into the attract mode.
        landmark="A 3D Space battle",
        landmark_timeout_seconds=120.0,
        nav=(("Load high score table", "N"),),
    ),
    # Other games with attract/demo modes worth adding (recipes TBD): Thrust
    # (discs/games/Disc024-Thrust.ssd) and Arcadians.
]
