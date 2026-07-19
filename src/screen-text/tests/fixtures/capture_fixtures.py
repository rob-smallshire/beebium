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

"""Recapture the committed screenshot fixtures from a running machine.

A development tool, not part of the library or its build. The library depends
on nothing outside its own directory; this script depends on the emulator and
its Python client, which is why the images it produces are committed rather
than generated at test time. Nothing in the build or the test suite runs it.

Run from the Python client's project directory so that uv resolves the client:

    cd clients/beebium-python-client
    uv run --extra imaging python \
        ../../src/screen-text/tests/fixtures/capture_fixtures.py

Pass --output-dirpath to write elsewhere than alongside this script. See
README.md in this directory for the geometry these images are expected to
have, and what to check if a recapture differs.
"""

import argparse
import pathlib
import sys
import time

from beebium.client import Beebium
from beebium.client.screen import screen_contains

REPO_DIRPATH = pathlib.Path(__file__).resolve().parents[4]
ROMS_DIRPATH = REPO_DIRPATH / "roms"

PROGRAM = [
    "10 REM SCREEN TEXT",
    "20 FOR I%=1 TO 3",
    '30 PRINT "LINE ";I%',
    "40 NEXT I%",
    "50 END",
]

# COLOUR 129 swaps the background and COLOUR 0 the foreground, which is how the
# BBC produces inverse text.
INVERSE_COMMAND = 'COLOUR 129:COLOUR 0:PRINT "INVERSE TEXT"'

TYPING_DEADLINE_SECONDS = 15.0
SETTLE_SECONDS = 1.5


def drain(bbc, deadline=TYPING_DEADLINE_SECONDS):
    """Wait for the type-ahead queue to empty, reporting rather than hanging."""
    started = time.monotonic()
    while time.monotonic() - started < deadline:
        status = bbc.keyboard.typing_status()
        if getattr(status, "pending_characters", 0) == 0:
            return True
        time.sleep(0.05)
    print(f"    still typing after {deadline}s", file=sys.stderr)
    return False


def send(bbc, text):
    """Type a line and wait for the machine to consume it."""
    # run_until_or_timeout leaves the machine stopped, and keystrokes are only
    # consumed while it runs, so resume before typing or this waits for ever.
    if not bbc.debugger.is_running:
        bbc.debugger.run()
    bbc.keyboard.type(text)
    bbc.keyboard.press_return()
    drain(bbc)
    time.sleep(0.4)


def capture(bbc, out_filepath):
    """Capture one frame, reporting the geometry it was captured with."""
    frame = bbc.video.capture_frame(timeout=10.0)
    print(f"{out_filepath.name}: {frame.width}x{frame.height} "
          f"frame #{frame.frame_number}")
    for region in frame.regions:
        print(f"    lines {region.start_line}-{region.end_line} "
              f"pixel_width={region.pixel_width}")
    frame.save_png(str(out_filepath))
    return frame


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output-dirpath",
        type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parent,
        help="where to write the images (default: alongside this script)",
    )
    parser.add_argument(
        "--server-filepath",
        type=pathlib.Path,
        default=REPO_DIRPATH / "build" / "src" / "server" / "beebium-model-b",
        help="the server executable to launch",
    )
    args = parser.parse_args(argv)
    args.output_dirpath.mkdir(parents=True, exist_ok=True)

    with Beebium.launch(
        mos_filepath=str(ROMS_DIRPATH / "acorn-mos_1_20.rom"),
        basic_filepath=str(ROMS_DIRPATH / "bbc-basic_2.rom"),
        server_filepath=str(args.server_filepath),
    ) as bbc:
        bbc.debugger.ensure_running()
        bbc.run_until_or_timeout(
            lambda: screen_contains(bbc, ">"), emulated_seconds=5.0
        )

        for line in PROGRAM:
            send(bbc, line)

        send(bbc, "MODE4")
        time.sleep(1.0)
        send(bbc, "LIST")
        time.sleep(SETTLE_SECONDS)
        capture(bbc, args.output_dirpath / "mode4-listing.png")

        send(bbc, "MODE4")
        time.sleep(1.0)
        send(bbc, INVERSE_COMMAND)
        time.sleep(SETTLE_SECONDS)
        capture(bbc, args.output_dirpath / "mode4-inverse.png")

    return 0


if __name__ == "__main__":
    sys.exit(main())
