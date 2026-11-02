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

"""Storage round trips through the guest, the image file and a relaunch (#247).

Each row is a filing system on a storage device with an image format. The
guest saves a file long enough to cross tracks (and, on a double-sided ADFS L
disc, sides) and spools a text file to a fresh blank image. The server shuts
down, and oaknut, an implementation independent of Beebium, reads both files
from the image file. A new server then runs on the same image and the guest
reads them back.

The DFS rows marked as formatting first fill the blank image with junk, and
the guest formats it with dfs_format.bas (OSWORD &7F, the 8271 Format Track
command, which the 1770 DFS emulates) before writing to it, so a format in
the guest must reach the image file too (#249).

A check made only inside one guest session reads back the emulator's own
in-memory track, and passes even when nothing correct reaches the file: #246
went unnoticed that way for every ADFS floppy write.
"""

from __future__ import annotations

import itertools
from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path

import pytest
from oaknut.adfs import ADFS
from oaknut.dfs import (
    ACORN_DFS_80T_DOUBLE_SIDED_INTERLEAVED,
    ACORN_DFS_80T_SINGLE_SIDED,
    DFS,
)

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError

# The file the guest saves: long enough to cross two DFS tracks (2.5K each)
# and both sides of an ADFS L track (4K a side).
BIG_ADDRESS = 0x3000
BIG_LENGTH = 0x2200
LOAD_ADDRESS = 0x5800
BIG_DATA = bytes((i * 7 + (i >> 8) * 13 + 1) & 0xFF for i in range(BIG_LENGTH))
HELP_TEXT = "OS 1.20"  # every *HELP on a Model B prints the MOS version
TIMEOUT = 60.0
FORMAT_TIMEOUT = 240.0  # 80 tracks, a revolution or two each, at real time
FORMATTER_FILEPATH = Path(__file__).parent / "dfs_format.bas"
FORMAT_FILL = 0xE5  # what an 8271-style format leaves in every sector
DFS_SECTORS = 800  # 80 tracks of 10


@dataclass(frozen=True)
class Row:
    id: str
    sideways: dict[int, str]  # slot -> ROM filename
    args: tuple[str, ...]  # server arguments other than the ROMs and the image
    select: str  # typed to select the filing system and the disc
    big_names: tuple[str, ...]  # where the guest saves the big file
    text_name: str
    # Makes the blank image; given a running server when it needs one.
    make_image: Callable[[Beebium | None, Path], Path] = field(compare=False)
    image_args: Callable[[Path], list[str]] = field(compare=False)
    read_image: Callable[[Path, str], bytes] = field(compare=False)
    needs_server_for_blank: bool = True
    format_in_guest: bool = False


def _blank(label: str, filename: str) -> Callable[[Beebium | None, Path], Path]:
    def make(bbc: Beebium | None, dirpath: Path) -> Path:
        assert bbc is not None
        return bbc.disc.create_disc(label, dirpath / filename)
    return make


def _floppy(image_filepath: Path) -> list[str]:
    return ["--floppy", f"0:{image_filepath}"]


def _read_dfs(disc_format) -> Callable[[Path, str], bytes]:
    def read(image_filepath: Path, name: str) -> bytes:
        side = 0
        if name.startswith(":2."):
            side, name = 1, name[3:]
        with DFS.from_file(image_filepath, disc_format, side=side, read_only=True) as dfs:
            return bytes(dfs.path(f"$.{name}").read_bytes())
    return read


def _read_adfs(image_filepath: Path, name: str) -> bytes:
    with ADFS.from_file(image_filepath, read_only=True) as adfs:
        return bytes(adfs.path(f"$.{name}").read_bytes())


def _make_hard_disc(bbc: Beebium | None, dirpath: Path) -> Path:
    image_filepath = dirpath / "scsi0.dat"
    with ADFS.create_file(image_filepath, capacity="10MB", title="RoundTrip"):
        pass
    return image_filepath


_ADFS_FLOPPY = dict(
    sideways={14: "acorn-dfs_2_26.rom", 13: "acorn-adfs_1_30.rom"},
    args=("--fdc", "acorn-1770"),
    select="*ADFS\r*MOUNT\r",
    big_names=("BIG",),
    text_name="TEXT",
    image_args=_floppy,
    read_image=_read_adfs,
)

ROWS = [
    Row(
        id="dfs-8271-ssd",
        sideways={14: "acorn-dnfs_1_20.rom"},
        args=("--fdc", "acorn-8271"),
        select="*DISC\r",
        big_names=("BIG",),
        text_name="TEXT",
        make_image=_blank("Acorn DFS 80T SS 200K", "disc.ssd"),
        image_args=_floppy,
        read_image=_read_dfs(ACORN_DFS_80T_SINGLE_SIDED),
    ),
    Row(
        id="dfs-8271-ssd-formatted",
        sideways={14: "acorn-dnfs_1_20.rom"},
        args=("--fdc", "acorn-8271"),
        select="*DISC\r",
        big_names=("BIG",),
        text_name="TEXT",
        make_image=_blank("Acorn DFS 80T SS 200K", "disc.ssd"),
        image_args=_floppy,
        read_image=_read_dfs(ACORN_DFS_80T_SINGLE_SIDED),
        format_in_guest=True,
    ),
    Row(
        id="dfs-1770-ssd-formatted",
        sideways={14: "acorn-dfs_2_26.rom"},
        args=("--fdc", "acorn-1770"),
        select="*DISC\r",
        big_names=("BIG",),
        text_name="TEXT",
        make_image=_blank("Acorn DFS 80T SS 200K", "disc.ssd"),
        image_args=_floppy,
        read_image=_read_dfs(ACORN_DFS_80T_SINGLE_SIDED),
        format_in_guest=True,
    ),
    Row(
        id="dfs-1770-ssd",
        sideways={14: "acorn-dfs_2_26.rom"},
        args=("--fdc", "acorn-1770"),
        select="*DISC\r",
        big_names=("BIG",),
        text_name="TEXT",
        make_image=_blank("Acorn DFS 80T SS 200K", "disc.ssd"),
        image_args=_floppy,
        read_image=_read_dfs(ACORN_DFS_80T_SINGLE_SIDED),
    ),
    Row(
        id="dfs-1770-dsd",
        sideways={14: "acorn-dfs_2_26.rom"},
        args=("--fdc", "acorn-1770"),
        select="*DISC\r",
        big_names=("BIG", ":2.BIG"),  # drive 2 is the disc's second side
        text_name="TEXT",
        make_image=_blank("Acorn DFS 80T DS 400K", "disc.dsd"),
        image_args=_floppy,
        read_image=_read_dfs(ACORN_DFS_80T_DOUBLE_SIDED_INTERLEAVED),
    ),
    Row(id="adfs-1770-s", make_image=_blank("Acorn ADFS S 160K", "disc.ads"), **_ADFS_FLOPPY),
    Row(id="adfs-1770-m", make_image=_blank("Acorn ADFS M 320K", "disc.adm"), **_ADFS_FLOPPY),
    Row(id="adfs-1770-l", make_image=_blank("Acorn ADFS L 640K", "disc.adl"), **_ADFS_FLOPPY),
    Row(
        id="adfs-scsi",
        sideways={13: "acorn-adfs_1_30.rom"},
        args=("--acorn-scsi",),
        select="*ADFS\r*MOUNT 0\r",
        big_names=("BIG",),
        text_name="TEXT",
        make_image=_make_hard_disc,
        needs_server_for_blank=False,  # the server has no hard disc templates
        image_args=lambda image_filepath: ["--scsi-hdd", f"0:{image_filepath}"],
        read_image=_read_adfs,
    ),
]


def _launch(row: Row, roms_dirpath: Path, server: Path | None, extra: list[str]) -> Beebium:
    args = list(row.args)
    for slot, rom_filename in row.sideways.items():
        rom_filepath = roms_dirpath / rom_filename
        if not rom_filepath.exists():
            pytest.skip(f"ROM not found: {rom_filepath}")
        args += ["--sideways", f"slot={slot}:type=rom:image={rom_filepath}"]
    try:
        return Beebium.launch(server=server, extra_args=args + extra, startup_timeout=30.0)
    except ServerNotFoundError as e:
        pytest.skip(str(e))


_sentinel_numbers = itertools.count()


def _run_and_wait(bbc: Beebium, commands: str, label: str, timeout: float = TIMEOUT) -> None:
    """Type `commands`, then a PRINT that shows a sentinel only once they are done.

    The typed PRINT line shows the sentinel split in two, so it matches only
    when the PRINT itself runs, after every command before it has returned.
    Each sentinel is numbered, so an earlier one still on the screen never
    matches.
    """
    sentinel = f"{label}{next(_sentinel_numbers)}"
    half = len(label) // 2
    bbc.keyboard.type(f'{commands}PRINT "{sentinel[:half]}";"{sentinel[half:]}"\r')
    bbc.expect(sentinel, timeout=timeout)


@pytest.mark.parametrize("row", ROWS, ids=[row.id for row in ROWS])
def test_files_written_in_the_guest_reach_the_image_and_survive_a_relaunch(
    row: Row,
    beebium_roms_dirpath: Path,
    beebium_server_filepath: Path | None,
    tmp_path: Path,
) -> None:
    # A first server makes the blank, so a row starts from the same template
    # a user's would, then the machine that writes to it starts on it.
    if not row.needs_server_for_blank:
        image_filepath = row.make_image(None, tmp_path)
    else:
        with _launch(row, beebium_roms_dirpath, beebium_server_filepath, []) as maker:
            image_filepath = row.make_image(maker, tmp_path)

    if row.format_in_guest:
        # Junk, not a blank: only a format in the guest makes it a disc.
        size = image_filepath.stat().st_size
        image_filepath.write_bytes(bytes((i * 151 + 89) & 0xFF for i in range(size)))

    with _launch(row, beebium_roms_dirpath, beebium_server_filepath,
                 row.image_args(image_filepath)) as bbc:
        bbc.wait_until_ready()
        _run_and_wait(bbc, row.select, "SELECTED")
        if row.format_in_guest:
            # Typed without its comment lines, which only slow the typing.
            program = "".join(
                f"{line}\r" for line in FORMATTER_FILEPATH.read_text().splitlines()
                if line.split(" ", 1)[1:] and not line.split(" ", 1)[1].startswith("REM")
            )
            _run_and_wait(bbc, f"NEW\r{program}", "ENTERED", FORMAT_TIMEOUT)
            # Nothing is typed while it runs, so the run is fast-forwarded.
            # It prints its last line in two parts, so the listing never
            # shows it whole; Caps Lock upper-cases what was typed.
            bbc.keyboard.type("CLS\rRUN\r")
            bbc.keyboard.wait_until_typing_complete()
            bbc.system.set_speed_multiplier(0.0)
            try:
                bbc.expect("FORMATTED", "FAILED", timeout=FORMAT_TIMEOUT)
            finally:
                bbc.system.set_speed_multiplier(1.0)
            screen = bbc.video.screen_text().text
            assert "FORMATTED" in screen and "FAILED" not in screen, screen
        bbc.memory.address.bus.write(BIG_ADDRESS, BIG_DATA)
        saves = "".join(f"*SAVE {name} {BIG_ADDRESS:X} +{BIG_LENGTH:X}\r" for name in row.big_names)
        _run_and_wait(bbc, f"{saves}*SPOOL {row.text_name}\r*HELP\r*SPOOL\r", "WRITTEN")
    # The server has shut down, so its writes are in the image file.

    for name in row.big_names:
        assert row.read_image(image_filepath, name) == BIG_DATA, f"{name} on the image"
    text = row.read_image(image_filepath, row.text_name)
    assert HELP_TEXT.encode() in text, f"{row.text_name} on the image: {text!r}"
    if row.format_in_guest:
        # The last sector holds no file, so it shows the format's fill.
        image = image_filepath.read_bytes()
        assert len(image) == DFS_SECTORS * 256
        assert image[-256:] == bytes([FORMAT_FILL]) * 256, "the format did not reach the image"

    with _launch(row, beebium_roms_dirpath, beebium_server_filepath,
                 row.image_args(image_filepath)) as bbc:
        bbc.wait_until_ready()
        _run_and_wait(bbc, row.select, "SELECTED")
        for name in row.big_names:
            bbc.memory.address.bus.write(LOAD_ADDRESS, bytes(BIG_LENGTH))
            _run_and_wait(bbc, f"*LOAD {name} {LOAD_ADDRESS:X}\r", "LOADED")
            assert bbc.memory.address.peek.read(LOAD_ADDRESS, BIG_LENGTH) == BIG_DATA, (
                f"{name} read back in the guest:\n{bbc.video.screen_text().text}"
            )
        # *LOAD, not *TYPE: *TYPE comes from the DFS utilities, which the
        # hard disc row has no ROM for.
        bbc.memory.address.bus.write(LOAD_ADDRESS, bytes(len(text)))
        _run_and_wait(bbc, f"*LOAD {row.text_name} {LOAD_ADDRESS:X}\r", "LOADED")
        assert bbc.memory.address.peek.read(LOAD_ADDRESS, len(text)) == text, (
            f"{row.text_name} read back in the guest"
        )


# The write-inactivity flush (DiscWriteBack): about 0.25 s of emulated time
# after the last write, well short of the drive motor's spin-down of about
# 2 s, the controller writes its dirty tracks to the image file.
AFTER_SAVE_SECONDS = 0.6
FLUSH_ROWS = [row for row in ROWS if row.id in ("dfs-8271-ssd", "dfs-1770-ssd", "adfs-1770-l")]


@pytest.mark.parametrize("row", FLUSH_ROWS, ids=[row.id for row in FLUSH_ROWS])
def test_a_save_reaches_the_image_file_while_the_motor_still_runs(
    row: Row,
    beebium_roms_dirpath: Path,
    beebium_server_filepath: Path | None,
    tmp_path: Path,
) -> None:
    """Writes reach the image on the controller's own settle timer.

    Not at motor-off, not at eject, not at shutdown: the save is on the host,
    readable by another program, while the server runs and the motor turns.
    The time is emulated, stepped by the debugger, so a slow host cannot
    stretch it past the motor's spin-down.
    """
    with _launch(row, beebium_roms_dirpath, beebium_server_filepath, []) as maker:
        image_filepath = row.make_image(maker, tmp_path)

    with _launch(row, beebium_roms_dirpath, beebium_server_filepath,
                 row.image_args(image_filepath)) as bbc:
        bbc.wait_until_ready()
        _run_and_wait(bbc, row.select, "SELECTED")
        bbc.memory.address.bus.write(BIG_ADDRESS, BIG_DATA)
        sentinel = f"SAVED{next(_sentinel_numbers)}"
        bbc.keyboard.type(f'*SAVE BIG {BIG_ADDRESS:X} +{BIG_LENGTH:X}\r'
                          f'PRINT "{sentinel[:3]}";"{sentinel[3:]}"\r')
        assert bbc.run_until_or_timeout(
            lambda: sentinel in bbc.video.screen_text().text, emulated_seconds=30.0
        ), f"*SAVE did not return:\n{bbc.video.screen_text().text}"

        bbc.run_for_emulated_seconds(AFTER_SAVE_SECONDS)
        assert bbc.disc.drive0.motor_on, "the motor stopped first, so the flush is not shown apart from it"
        assert row.read_image(image_filepath, "BIG") == BIG_DATA
