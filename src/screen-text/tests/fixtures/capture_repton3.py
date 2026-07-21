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

"""Capture Repton 3's menu screen and dump its soft font from RAM.

A development tool, not part of the build or the test suite. Boots the game
(discs/games/, not in the repository), pages the MODE 7 instructions, reaches
the MODE 5 splash, and reads the soft-font area &C00-&CFF -- which here DOES
hold the menu font, characters 224-249 redefined as a chunky A-Z (offset
+159). fonts/repton3.glyphs was written straight from that dump.

Unlike Thrust, whose font is a private blit table absent from &C00, Repton's
menu is proper VDU 23 soft characters. Its credits screen, reached by selecting
the game and waiting through the load, is the other way about: a thin font the
game blits from its own table, matching neither &C00 nor the ROM.
"""

import pathlib
import sys
import time

from beebium.client import Beebium
from beebium.client.keyboard import SHIFT_KEY

REPO_DIRPATH = pathlib.Path(__file__).resolve().parents[4]
ROMS_DIRPATH = REPO_DIRPATH / "roms"
DISC_FILEPATH = REPO_DIRPATH / "discs" / "games" / "Disc024-Repton3P.ssd"


def shift_break(bbc):
    bbc.keyboard.matrix_down(*SHIFT_KEY)
    time.sleep(0.3)
    bbc.keyboard.break_down()
    time.sleep(0.3)
    bbc.keyboard.break_up()
    time.sleep(0.5)
    bbc.keyboard.matrix_up(*SHIFT_KEY)


def tap(bbc, row, column, hold=0.15):
    bbc.keyboard.matrix_down(row, column)
    time.sleep(hold)
    bbc.keyboard.matrix_up(row, column)


def main(argv=None):
    out_dirpath = pathlib.Path(argv[0]) if argv else pathlib.Path(".")
    out_dirpath.mkdir(parents=True, exist_ok=True)

    if not DISC_FILEPATH.exists():
        print(f"missing {DISC_FILEPATH} (discs/games/ is not in the repository)",
              file=sys.stderr)
        return 1

    with Beebium.launch(
        mos_filepath=str(ROMS_DIRPATH / "acorn-mos_1_20.rom"),
        basic_filepath=str(ROMS_DIRPATH / "bbc-basic_2.rom"),
        server_filepath=str(REPO_DIRPATH / "build" / "src" / "server"
                            / "beebium-model-b"),
        extra_args=["--fdc", "acorn-1770", "--sideways",
                    f"14:rom:{ROMS_DIRPATH / 'acorn-dfs_2_26.rom'}"],
    ) as bbc:
        bbc.debugger.ensure_running()
        time.sleep(1.5)
        bbc.disc.drive(0).insert(str(DISC_FILEPATH))
        shift_break(bbc)
        time.sleep(8.0)

        for _ in range(7):          # page the MODE 7 instructions
            tap(bbc, 6, 2)          # SPACE, on the keyboard matrix
            time.sleep(1.2)
        time.sleep(6.0)             # let the splash draw its PLEASE SELECT

        frame = bbc.video.capture_frame(timeout=10.0)
        frame.save_png(str(out_dirpath / "repton3-select.png"))
        print(f"menu {frame.width}x{frame.height}", flush=True)

        font = bytes(bbc.memory.address.peek[0x0C00:0x0D00])
        (out_dirpath / "repton3_font.bin").write_bytes(font)
        defined = sum(1 for c in range(224, 256)
                      if any(font[(c - 224) * 8:(c - 224) * 8 + 8]))
        print(f"&C00 soft font: {defined}/32 codes defined (224-249 = A-Z)",
              flush=True)

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
