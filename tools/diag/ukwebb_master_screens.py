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

"""Capture every screen of the UKWEBB.NET Master diagnostic ROM as PNGs.

Boots a Master 128 with the ROM's 16K MOS half as the MOS and its bank 9
image in sideways slot 9, then saves the RAM test screen, the hardware test
summary and each submenu (K L M S A C R D X; three frames 2.5 s apart for
the Modes and Sound submenus, which change as they run). The ROM is private
and lives outside the repository; its directory defaults to the
BEEBIUM_UKWEBB_DIAG_DIRPATH environment variable.

The ROM is its own OS and scans the keyboard matrix itself, so each key is
held down for a while in emulated time rather than typed. All waits are in
emulated time at the machine's real speed.

Run it from the repository root with the Python client's environment and
its imaging extra (Pillow):

    uv run --project clients/beebium-python-client --extra imaging \\
        python tools/diag/ukwebb_master_screens.py --output-dirpath screens

Add --floppy <image> to have a disc in drive 0 for the Disc submenu's speed
measurement (a blank ADFS disc from the server's create-disc will do).
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

from beebium.client import Beebium
from beebium.client.keyboard_map import ESCAPE_KEY, SPACE_KEY

DIRPATH_VARIABLE = "BEEBIUM_UKWEBB_DIAG_DIRPATH"
MOS_FILENAME = "ukwebb_diag_master_16k.rom"
BANK_9_FILENAME = "ukwebb_diag_master_bank9.rom"

KEY_HOLD_SECONDS = 0.4
MENU_SETTLE_SECONDS = 1.0
MENU_SECONDS = 45.0
SUBMENU_SETTLE_SECONDS = 2.0
DISC_MEASURE_SECONDS = 12.0
FRAME_INTERVAL_SECONDS = 2.5
# A key the menu does nothing with, to leave a submenu that exits on any key
# without the menu then acting on it (SPACE there starts the tests again).
EXIT_KEY = "z"

# (key, file name stem, frames, keyboard test that exits on ESCAPE+SPACE)
SUBMENUS = [
    ("k", "02-keyboard", 1, True),
    ("l", "03-key-matrix", 1, True),
    ("m", "04-modes", 3, False),
    ("s", "05-sound", 3, False),
    ("a", "06-analogue", 1, False),
    ("c", "07-rtc-cmos", 1, False),
    ("r", "08-sideways-roms", 1, False),
    ("d", "09-disc-drive", 1, False),
    ("x", "10-extra-ram", 1, False),
]


def screen(bbc: Beebium) -> str:
    """The screen as Mode 7 text; empty while the ROM shows another mode."""
    try:
        return bbc.basic.read_screen_text()
    except IndexError:
        # read_screen_text reads Mode 7 rows, which another mode lacks.
        return ""


def run_until_screen_shows(bbc: Beebium, text: str, emulated_seconds: float) -> None:
    if not bbc.run_until_or_timeout(lambda: text in screen(bbc), emulated_seconds):
        raise SystemExit(
            f"The screen did not show {text!r} within {emulated_seconds} emulated "
            f"seconds; it showed:\n{screen(bbc)}"
        )


def hold(bbc: Beebium, *matrix_keys: tuple[int, int]) -> None:
    for row, column in matrix_keys:
        bbc.keyboard.matrix_down(row, column)
    try:
        bbc.run_for_emulated_seconds(KEY_HOLD_SECONDS)
    finally:
        for row, column in matrix_keys:
            bbc.keyboard.matrix_up(row, column)


def hold_key(bbc: Beebium, key: str) -> None:
    bbc.keyboard.key_down(key)
    try:
        bbc.run_for_emulated_seconds(KEY_HOLD_SECONDS)
    finally:
        bbc.keyboard.key_up(key)


def wait_for_menu(bbc: Beebium) -> None:
    run_until_screen_shows(bbc, "SPACE=Re-test", MENU_SECONDS)
    bbc.run_for_emulated_seconds(MENU_SETTLE_SECONDS)


def save(bbc: Beebium, output_dirpath: Path, stem: str) -> None:
    filepath = output_dirpath / f"{stem}.png"
    bbc.video.capture_frame().save_png(filepath)
    print(filepath)


def capture_screens(bbc: Beebium, output_dirpath: Path) -> None:
    run_until_screen_shows(bbc, "Hardware tests next", MENU_SECONDS)
    save(bbc, output_dirpath, "00-ram-tests")
    wait_for_menu(bbc)
    save(bbc, output_dirpath, "01-hardware-tests-summary")

    for key, stem, frames, exits_on_escape_space in SUBMENUS:
        hold_key(bbc, key)
        bbc.run_for_emulated_seconds(
            DISC_MEASURE_SECONDS if key == "d" else SUBMENU_SETTLE_SECONDS
        )
        if frames == 1:
            save(bbc, output_dirpath, stem)
        else:
            for index in range(frames):
                if index:
                    bbc.run_for_emulated_seconds(FRAME_INTERVAL_SECONDS)
                save(bbc, output_dirpath, f"{stem}-{'abc'[index]}")
        if exits_on_escape_space:
            hold(bbc, ESCAPE_KEY, SPACE_KEY)
        else:
            hold_key(bbc, EXIT_KEY)
        wait_for_menu(bbc)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument(
        "--rom-dirpath",
        type=Path,
        default=os.environ.get(DIRPATH_VARIABLE),
        help=f"The directory holding {MOS_FILENAME} and {BANK_9_FILENAME} "
        f"(default: ${DIRPATH_VARIABLE}).",
    )
    parser.add_argument(
        "--output-dirpath", type=Path, required=True, help="Where to save the PNGs."
    )
    parser.add_argument("--server", help="The beebium-master-128 server to run.")
    parser.add_argument("--floppy", type=Path, help="A disc image for drive 0.")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    if args.rom_dirpath is None:
        print(f"Give --rom-dirpath or set {DIRPATH_VARIABLE}.", file=sys.stderr)
        return 2
    rom_dirpath = Path(args.rom_dirpath)
    missing = [name for name in (MOS_FILENAME, BANK_9_FILENAME) if not (rom_dirpath / name).is_file()]
    if missing:
        print(f"{rom_dirpath} does not hold {', '.join(missing)}.", file=sys.stderr)
        return 2
    args.output_dirpath.mkdir(parents=True, exist_ok=True)

    extra_args = [
        "--preset",
        "master-128",
        "--mos",
        str(rom_dirpath / MOS_FILENAME),
        "--sideways",
        f"slot=9:type=rom:image={rom_dirpath / BANK_9_FILENAME}",
    ]
    if args.floppy is not None:
        extra_args += ["--floppy", f"0:{args.floppy}"]
    with Beebium.launch(server=args.server, variant="master-128", extra_args=extra_args) as bbc:
        capture_screens(bbc, args.output_dirpath)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
