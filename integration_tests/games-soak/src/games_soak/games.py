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
record. Landmark/navigation text is matched against the Mode 7 screen via
beebium.client.screen.screen_contains, exactly as the source tests do -- every
game here boots through a Mode 7 menu before switching to its own mode.

All three currently share the same Model B base (MOS 1.20, BASIC 2, Acorn 1770
DFS 2.26 in slot 14); Tube games add --tube-65c02. Each game relaunches its own
server, so heterogeneous machine configs (Tube vs not) coexist and the full
server + frontend lifecycle is exercised every iteration.
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
    # For Tube games: wait for this banner after reset before booting the disc.
    boot_banner: str | None = None
    # Typed after reset + mount to boot the disc, e.g. "*EXEC !BOOT\r". (Runtime
    # keyboard-link autoboot on BREAK does not currently work in the emulator --
    # tracked separately -- so games boot by typing their !BOOT invocation.)
    boot_command: str | None = None

    # --- navigation to a running/attract state ---
    # Confirms the game booted before navigating; None skips the check.
    landmark: str | None = None
    # (wait_text, key) pairs: wait until wait_text appears, then type key.
    nav: tuple[tuple[str, str], ...] = ()
    # run_until_or_timeout budget applied to the landmark and each nav step.
    landmark_timeout_seconds: float = 60.0
    landmark_chunk_seconds: float = 0.5

    # Real-time keypresses to drive an attract/demo mode, pressed AFTER nav with
    # the machine running, spaced by attract_interval_seconds. Used for games
    # (e.g. Galaforce) that advance through instruction pages on a timer rather
    # than at a distinct per-page landmark.
    attract_keys: tuple[str, ...] = ()
    attract_interval_seconds: float = 1.0

    # How long to let the game run in real time while watching for a freeze.
    run_minutes: float = 3.0


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
        boot_command="*EXEC !BOOT\r",
        # The boot sequence itself is the landmark walk; no separate banner.
        nav=_REVS_NAV,
        landmark_timeout_seconds=60.0,
        landmark_chunk_seconds=0.5,
        run_minutes=3.0,
    ),
    Game(
        name="Chuckie Egg 2023",
        tube_args=("--tube-65c02",),
        disc="tests/assets/discs/chuckieEgg2023.ssd",
        boot_banner="Acorn TUBE",
        boot_command="*EXEC !BOOT\r",
        landmark="A game of skill",
        # The title screen waits for a keypress; pressing space starts play.
        nav=(("A game of skill", " "),),
        # The game takes a long time to load through the Tube.
        landmark_timeout_seconds=300.0,
        landmark_chunk_seconds=5.0,
        run_minutes=3.0,
    ),
    Game(
        name="Elite",
        tube_args=("--tube-65c02",),
        disc="tests/assets/discs/Disc999-EliteSNG45.ssd",
        boot_banner="Acorn TUBE",
        boot_command="*RUN !BOOT\r",
        landmark="6502 Second Processor ELITE",
        # The !BOOT loader shows the banner then switches to a graphics mode;
        # confirming the banner and letting it run is enough for the soak. Elite
        # enters an attract mode on its own, so it exercises the machine.
        nav=(),
        landmark_timeout_seconds=60.0,
        landmark_chunk_seconds=1.0,
        run_minutes=3.0,
    ),
    Game(
        name="Galaforce",
        disc="discs/games/Disc025-Galaforce.ssd",
        boot_command="*EXEC !BOOT\r",
        # Mode 7 instructions screen; unique opening line of the story text.
        landmark="In the midst of the",
        # Press SPACE six times at ~1s intervals to advance through the
        # instruction pages to the hi-score table and self-play attract mode.
        attract_keys=(" ", " ", " ", " ", " ", " "),
        attract_interval_seconds=1.0,
        run_minutes=3.0,
    ),
    Game(
        name="Galaforce 2",
        disc="discs/games/Disc039-Galaforce2PIAS6.ssd",
        boot_command="*EXEC !BOOT\r",
        # Mode 7 instructions screen; unique opening line of the story text.
        landmark="Everything that Galaforce was",
        # Press SPACE four times at ~1s intervals to reach hi-scores/attract.
        attract_keys=(" ", " ", " ", " "),
        attract_interval_seconds=1.0,
        run_minutes=3.0,
    ),
    # Other games with attract/demo modes worth adding (recipes TBD): Thrust
    # (discs/games/Disc024-Thrust.ssd) and Arcadians.
]
