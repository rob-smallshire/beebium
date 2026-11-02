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

"""ADFS writes to a floppy through the 1770, against a real server (#246).

ADFS works in double density. The first write to a fresh ADFS L image, the
close of a *SPOOL file, used to hang, because the 1770 wrote each MFM sector
without the A1 syncs before its data mark and could not read it back. The
spooled file must read back in the guest and reach the image file.
"""

from __future__ import annotations

import time
from collections.abc import Iterator
from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError

ADFS_ROM_FILENAME = "acorn-adfs_1_30.rom"
ADFS_SLOT = 13  # below DFS in 14, so *ADFS selects it
HELP_TITLE = "Advanced DFS 1.30"
SECTOR_SIZE = 256
ROOT_DIRECTORY_OFFSET = 2 * SECTOR_SIZE  # ADFS L: $ at sector 2
FIRST_ENTRY_OFFSET = ROOT_DIRECTORY_OFFSET + 5  # after the sequence byte and "Hugo"


@pytest.fixture
def bbc_adfs_floppy(
    mos_filepath: Path,
    basic_filepath: Path | None,
    beebium_server_filepath: Path | None,
    beebium_roms_dirpath: Path,
    dfs_1770_rom_filepath: Path,
) -> Iterator[Beebium]:
    """A Model B with a 1770, DFS 2.26 and ADFS 1.30, its drives empty."""
    adfs_rom_filepath = beebium_roms_dirpath / ADFS_ROM_FILENAME
    if not adfs_rom_filepath.exists():
        pytest.skip(f"ADFS 1.30 ROM not found: {adfs_rom_filepath}")
    try:
        with Beebium.launch(
            mos_filepath=mos_filepath,
            basic_filepath=basic_filepath,
            server=beebium_server_filepath,
            extra_args=[
                "--fdc", "acorn-1770",
                "--sideways", f"slot=14:type=rom:image={dfs_1770_rom_filepath}",
                "--sideways", f"slot={ADFS_SLOT}:type=rom:image={adfs_rom_filepath}",
            ],
            startup_timeout=20.0,
        ) as bbc:
            yield bbc
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def _wait_for_image(image_filepath: Path, timeout: float = 20.0) -> bytes:
    """The image once the spooled text and FRED's directory entry are in it.

    The controller writes a track back to the file shortly after the guest
    stops writing, so poll for it.
    """
    deadline = time.monotonic() + timeout
    while True:
        image = image_filepath.read_bytes()
        name = bytes(b & 0x7F for b in image[FIRST_ENTRY_OFFSET:FIRST_ENTRY_OFFSET + 4])
        if HELP_TITLE.encode() in image and name == b"FRED":
            return image
        if time.monotonic() > deadline:
            return image
        time.sleep(0.2)


def test_a_spool_file_on_a_fresh_adl_reads_back(bbc_adfs_floppy: Beebium, tmp_path: Path) -> None:
    bbc = bbc_adfs_floppy
    image_filepath = bbc.disc.create_disc("Acorn ADFS L 640K", tmp_path / "test.adl")
    blank = image_filepath.read_bytes()
    assert HELP_TITLE.encode() not in blank
    bbc.disc.drive(0).insert(image_filepath)

    bbc.wait_until_ready()
    bbc.keyboard.type("*ADFS\r*MOUNT\r*SPOOL FRED\r*HELP\r*SPOOL\r")
    # The close is the first write; it used to hang there. Only once it
    # returns do the screen clear, the file type out and the sentinel print
    # (the typed line shows it split, so only the PRINT shows it whole).
    bbc.keyboard.type('CLS\r*TYPE FRED\rPRINT "READ";"BACK"\r')
    bbc.expect("READBACK", timeout=60.0)
    assert HELP_TITLE in bbc.video.screen_text().text, "FRED did not type out the *HELP text"

    image = _wait_for_image(image_filepath)
    assert HELP_TITLE.encode() in image, "the spooled text never reached the image file"
    name = bytes(b & 0x7F for b in image[FIRST_ENTRY_OFFSET:FIRST_ENTRY_OFFSET + 4])
    assert name == b"FRED", "FRED is not the first entry in $ on the image"
