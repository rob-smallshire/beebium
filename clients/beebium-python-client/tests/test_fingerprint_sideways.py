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

"""Dabs Press Fingerprint installs into slot-15 sideways RAM on model-b-atpl-sidewise.

This is the end-to-end proof for the ATPL Sidewise board's defining feature and
the close-out for issue #72. Fingerprint's Model B install path (menu option 1)
loads its ROM image with a bare ``*LOAD SMON FFFF8000`` -- a write straight to
&8000-&BFFF while BASIC, not the target bank, is the paged ROM. It never pages
the target bank in first, so it only works on a board whose writes to the
sideways region reach RAM regardless of ROMSEL. The ATPL Sidewise board is
exactly that: slot 15 (its only RAM slot) is write-through -- a write to
&8000-&BFFF lands in slot-15 RAM whatever ROM is paged, while reads stay
ROMSEL-gated.

The test boots Fingerprint on ``model-b-atpl-sidewise``, drives its menu to
install the full sideways-RAM build into bank 15, and then proves the install
took by asking the MOS for help: ``*HELP FI.`` only lists Fingerprint's commands
if slot-15 RAM now holds a ROM the MOS scans as a service ROM -- i.e. only if the
write-through actually delivered the image. That second step is the load-bearing
assertion; the "Installed" banner alone is just the game's own optimism.

The menu wants the bank as a single hex nibble, so bank 15 is entered as ``F``
(typing "15" yields "Invalid hex"). Navigation polls the screen-text API rather
than blind-timing keypresses, following the Firetrack auto-boot tests.
"""

from __future__ import annotations

import hashlib
import time
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ScreenExpectTimeout, ServerNotFoundError

# Dabs Press Fingerprint (David Spencer / Dabs Press, 1987), from the issue #72
# report (stardot download file id 121282). Committed space-free, consistent
# with the other game discs here and sidestepping the DiscUrl spaced-path bug.
FINGERPRINT_DISC_FILENAME = "DabsPressFingerprint.ssd"
FINGERPRINT_DISC_SHA256 = "baeccaf6b0cf6135818c7f6aeddd0448bd27eb1534412f828d870090f802271d"

# Menu landmarks (Fingerprint Menu V2.0, drawn in MODE 1).
MENU_OPTION_1 = "Install full Sideways RAM"
BANK_PROMPT = "Which sideways RAM bank"
INSTALLED_BANNER = "Installed. *HELP FI. for commands"

# Bank 15 is entered as the hex nibble 'F'.
BANK_15_KEY = "F"

# Fingerprint's own *HELP FI. output -- proof the service ROM in slot-15 RAM ran.
HELP_BANNER = "Fingerprint 3.22"
HELP_COMMAND = "Set breakpoint"


@pytest.fixture(scope="module")
def fingerprint_disc_filepath() -> Path:
    """The committed Fingerprint disc, its integrity pinned by SHA-256."""
    repo_root = Path(__file__).parents[3]
    path = repo_root / "tests" / "assets" / "discs" / FINGERPRINT_DISC_FILENAME
    if not path.exists():
        pytest.skip(f"Fingerprint disc image not found: {path}")
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    assert digest == FINGERPRINT_DISC_SHA256, (
        f"Fingerprint disc {path} has SHA-256 {digest}, expected {FINGERPRINT_DISC_SHA256}"
    )
    return path


@pytest.fixture
def bbc_fingerprint(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
    dfs_1770_rom_filepath: Path,
    fingerprint_disc_filepath: Path,
) -> Beebium:
    """model-b-atpl-sidewise, DFS in slot 13, slot 15 as RAM, auto-booting Fingerprint.

    The board reserves slot 15 for RAM and defaults its language slot to 14, so
    BASIC lands at 14 and DFS is placed at 13 -- exactly the shipped
    model-b-atpl-sidewise-disc topology.
    """
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server=beebium_server_filepath,
            variant="model-b-atpl-sidewise",
            extra_args=[
                "--fdc",
                "acorn-1770",
                "--sideways",
                f"slot=13:type=rom:image={dfs_1770_rom_filepath}",
                "--sideways",
                "slot=15:type=ram",
                "--floppy",
                f"0:{fingerprint_disc_filepath}",
                "--auto-boot",
            ],
            startup_timeout=20.0,
        ) as bbc:
            bbc.debugger.ensure_running()
            yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def _drive(bbc: Beebium, keys: str, landmark: str, *, timeout: float = 20.0, attempts: int = 4) -> None:
    """Type `keys`, wait for `landmark`; re-drive if the press was dropped.

    A press dropped on a slow host leaves the screen on the prior landmark, so
    the wait times out and the keys are re-sent. The wait returns the instant
    the landmark appears, so a press that did register is never doubled.
    """
    deadline = time.monotonic() + timeout
    last_error: ScreenExpectTimeout | None = None
    for _ in range(attempts):
        bbc.keyboard.type(keys)
        remaining = max(3.0, deadline - time.monotonic())
        try:
            bbc.expect(landmark, timeout=remaining)
            return
        except ScreenExpectTimeout as error:
            last_error = error
            if time.monotonic() >= deadline:
                break
    assert last_error is not None
    raise last_error


def test_fingerprint_installs_into_slot15_sideways_ram(bbc_fingerprint: Beebium) -> None:
    """Install Fingerprint into bank 15 and prove the service ROM answers *HELP."""
    bbc = bbc_fingerprint

    # !BOOT chains INTRO, which draws its menu (self-correcting PAGE first).
    bbc.expect(MENU_OPTION_1, timeout=30.0)

    # Option 1: install the full sideways-RAM build.
    _drive(bbc, "1\r", BANK_PROMPT, timeout=15.0)

    # Bank 15 == hex 'F'. The install performs *LOAD SMON FFFF8000 with BASIC
    # paged in; on the ATPL board the write-through routes it to slot-15 RAM.
    _drive(bbc, f"{BANK_15_KEY}\r", INSTALLED_BANNER, timeout=20.0)

    # Criterion 1: the game reports the install completed.
    assert INSTALLED_BANNER in bbc.video.screen_text().text

    # Criterion 2 (load-bearing): the MOS scans slot 15 as a service ROM and
    # Fingerprint answers *HELP FI. with its command listing -- only possible
    # if the write-through actually delivered the ROM image into slot-15 RAM.
    _drive(bbc, "*HELP FI.\r", HELP_BANNER, timeout=15.0)
    help_text = bbc.video.screen_text().text
    assert HELP_BANNER in help_text, help_text
    assert HELP_COMMAND in help_text, help_text
