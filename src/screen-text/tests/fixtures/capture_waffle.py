#!/usr/bin/env python3
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

"""Capture screens from Waffle, a real game, for the test corpus.

A development tool, not part of the library or its build. Nothing in the build
or the test suite runs it; the images it produces are committed.

Waffle is by Chris Bradburne, after the web game at wafflegame.net. It earns
its place because its instruction screens flow text around graphics, in the
standard Acorn font on the character grid, which makes them the first test of
this library against software written by somebody else entirely.

The disc lives in `discs/games/`, which is not in the repository, so this
cannot be run from a clean checkout. See README.md in screens/ for what the
captures are and what they showed.

    cd clients/beebium-python-client
    uv run --extra imaging python \
        ../../src/screen-text/tests/fixtures/capture_waffle.py

The navigation is: boot, wait for the title, press Y at "Instructions?", then
a key through four instruction screens and into the game. Each screen needs
time to draw; the waits below are generous rather than tuned, this being run
by hand and rarely.
"""

import argparse
import pathlib
import subprocess
import sys
import time

from beebium.client import Beebium
from beebium.client.keyboard import SHIFT_KEY

REPO_DIRPATH = pathlib.Path(__file__).resolve().parents[4]
ROMS_DIRPATH = REPO_DIRPATH / "roms"
DISC_FILEPATH = REPO_DIRPATH / "discs" / "games" / "Disc165-Waffle.ssd"
CLI_FILEPATH = REPO_DIRPATH / "build" / "src" / "screen-text" / "screentext"

DRAW_SECONDS = 6.0

# What each capture is kept for; the rest of the sequence is passed through.
KEEP = {
    1: "waffle-title",
    3: "waffle-instructions-2",
    5: "waffle-instructions-4",
    6: "waffle-board",
}


def shift_break(bbc):
    """SHIFT-BREAK, which auto-boots a DFS disc."""
    bbc.keyboard.matrix_down(*SHIFT_KEY)
    time.sleep(0.3)
    bbc.keyboard.break_down()
    time.sleep(0.3)
    bbc.keyboard.break_up()
    time.sleep(0.5)
    bbc.keyboard.matrix_up(*SHIFT_KEY)


def grab(bbc, index, out_dirpath):
    """Capture a frame, keeping the ones worth keeping, and read it back."""
    frame = bbc.video.capture_frame(timeout=10.0)
    name = KEEP.get(index)
    if name is None:
        print(f"  {index}: {frame.width}x{frame.height} (not kept)", flush=True)
        return

    filepath = out_dirpath / f"{name}.png"
    frame.save_png(str(filepath))
    print(f"  {index}: {frame.width}x{frame.height} -> {filepath.name}",
          flush=True)

    # Read it straight back, which is how the navigation was worked out in the
    # first place, and how a recapture is checked.
    if CLI_FILEPATH.exists():
        result = subprocess.run(
            [str(CLI_FILEPATH), "read", str(filepath)],
            capture_output=True, text=True)
        lines = [line for line in result.stdout.splitlines() if line.strip()]
        for line in lines[:3]:
            print(f"      | {line}", flush=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-dirpath",
        type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parent / "screens",
        help="where to write the images",
    )
    args = parser.parse_args(argv)
    args.output_dirpath.mkdir(parents=True, exist_ok=True)

    if not DISC_FILEPATH.exists():
        print(f"missing {DISC_FILEPATH} (discs/games/ is not in the repository)",
              file=sys.stderr)
        return 1

    with Beebium.launch(
        mos_filepath=str(ROMS_DIRPATH / "acorn-mos_1_20.rom"),
        basic_filepath=str(ROMS_DIRPATH / "bbc-basic_2.rom"),
        server_filepath=str(REPO_DIRPATH / "build" / "src" / "server"
                            / "beebium-model-b"),
        extra_args=["--fdc", "acorn-1770",
                    "--sideways",
                    f"14:rom:{ROMS_DIRPATH / 'acorn-dfs_2_26.rom'}"],
    ) as bbc:
        bbc.debugger.ensure_running()
        time.sleep(1.5)
        bbc.disc.drive(0).insert(str(DISC_FILEPATH))
        shift_break(bbc)
        time.sleep(8.0)
        grab(bbc, 1, args.output_dirpath)

        bbc.keyboard.type("Y")          # "Instructions?"
        time.sleep(DRAW_SECONDS)
        grab(bbc, 2, args.output_dirpath)

        for index in range(3, 8):       # four instruction screens, then the game
            bbc.keyboard.press_space()
            time.sleep(DRAW_SECONDS)
            grab(bbc, index, args.output_dirpath)

    return 0


if __name__ == "__main__":
    sys.exit(main())
