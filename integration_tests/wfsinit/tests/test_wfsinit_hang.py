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

"""WFSINIT end-to-end test.

Boots a BBC Model B with ROM/RAM board, Tube 65C02, SCSI hard disc, and DFS
floppy, then runs the Acorn WFSINIT v0.90 utility through to completion.

The full prompt sequence:
    Drive number: 0
    Disc name   : WFSTEST
    Next drive  : <return>
    Date (dd/mm/yy): 25/10/85
    Please wait ....
    Password file (Y/N) : Y
    User name     1 : HOLMES
    User name     2 : MORIARTY
    Copy master directories (Y/N) : N
    ** Disc initialised **
    >

Run with:
    cd integration_tests/wfsinit
    uv run pytest -m slow tests/test_wfsinit_hang.py -v -s
"""

from __future__ import annotations

import time

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError
from beebium.client.screen import screen_contains, dump_screen, read_mode7_screen


def _wait_for_screen_text(bbc, text, timeout_seconds=120.0, poll_interval=0.5):
    """Wait until the given text appears on the MODE 7 screen."""
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        rows = read_mode7_screen(bbc)
        screen_text = "\n".join(rows)
        if text in screen_text:
            return True
        time.sleep(poll_interval)
    return False


@pytest.mark.slow
@pytest.mark.timeout(600)
def test_wfsinit_completes(
    server_filepath, mos_filepath, basic_filepath,
    anfs_filepath, adfs_filepath, dfs_filepath,
    wfsinit_ssd_filepath, scsi_hdd_filepath,
):
    """Run WFSINIT through all prompts to successful disc initialisation."""
    extra_args = [
        "--sideways", f"slot=9:type=rom:image={anfs_filepath}",
        "--sideways", f"slot=10:type=rom:image={adfs_filepath}",
        "--sideways", f"slot=11:type=rom:image={dfs_filepath}",
        "--fdc", "acorn-1770",
        "--floppy", f"0:{wfsinit_ssd_filepath}",
        "--acorn-scsi",
        "--scsi-hdd", f"0:{scsi_hdd_filepath}",
        "--station", "254",
        "--aun", "port=0",
        "--acorn-rtc", "layout=7bit-year-in-r7:time=1985-10-25T0000",
        "--tube-65c02",
    ]

    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server_filepath=server_filepath,
            extra_args=extra_args,
            startup_timeout=30.0,
        ) as bbc:
            # Boot to BASIC prompt.
            assert _wait_for_screen_text(bbc, ">", timeout_seconds=30), \
                f"Boot failed:\n{dump_screen(bbc)}"

            # CHAIN WFSINIT from floppy.
            bbc.keyboard.type('CHAIN "WFSINIT"\r')

            # Drive number: 0
            assert _wait_for_screen_text(bbc, "Drive number", timeout_seconds=60), \
                f"No 'Drive number' prompt:\n{dump_screen(bbc)}"
            bbc.keyboard.type("0\r")

            # Disc name: WFSTEST
            assert _wait_for_screen_text(bbc, "Disc name", timeout_seconds=30), \
                f"No 'Disc name' prompt:\n{dump_screen(bbc)}"
            bbc.keyboard.type("WFSTEST\r")

            # Next drive: <return> (accept default)
            assert _wait_for_screen_text(bbc, "Next drive", timeout_seconds=30), \
                f"No 'Next drive' prompt:\n{dump_screen(bbc)}"
            bbc.keyboard.type("\r")

            # Date: 25/10/85
            assert _wait_for_screen_text(bbc, "Date", timeout_seconds=30), \
                f"No 'Date' prompt:\n{dump_screen(bbc)}"
            bbc.keyboard.type("25/10/85\r")

            # Please wait .... (long delay for 20MB disc)
            assert _wait_for_screen_text(bbc, "Please wait", timeout_seconds=60), \
                f"No 'Please wait':\n{dump_screen(bbc)}"

            # Password file: Y
            assert _wait_for_screen_text(bbc, "Password file", timeout_seconds=300), \
                f"No 'Password file' prompt after 'Please wait':\n{dump_screen(bbc)}"
            bbc.keyboard.type("Y\r")

            # User name 1: HOLMES
            assert _wait_for_screen_text(bbc, "User name", timeout_seconds=30), \
                f"No 'User name' prompt:\n{dump_screen(bbc)}"
            bbc.keyboard.type("HOLMES\r")

            # User name 2: MORIARTY
            assert _wait_for_screen_text(bbc, "User name     2", timeout_seconds=30), \
                f"No second 'User name' prompt:\n{dump_screen(bbc)}"
            bbc.keyboard.type("MORIARTY\r")

            # User name 3: <return> (empty -- terminates user name list)
            assert _wait_for_screen_text(bbc, "User name     3", timeout_seconds=30), \
                f"No third 'User name' prompt:\n{dump_screen(bbc)}"
            bbc.keyboard.type("\r")

            # Copy master directories: N
            assert _wait_for_screen_text(bbc, "Copy master directories", timeout_seconds=30), \
                f"No 'Copy master directories' prompt:\n{dump_screen(bbc)}"
            bbc.keyboard.type("N\r")

            # ** Disc initialised **
            assert _wait_for_screen_text(bbc, "Disc initialised", timeout_seconds=120), \
                f"No 'Disc initialised' message:\n{dump_screen(bbc)}"

            print(f"WFSINIT completed successfully!")
            print(f"Screen:\n{dump_screen(bbc)}")

    except ServerNotFoundError as e:
        pytest.skip(str(e))
