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

"""Rebuild the blank tape templates in tapes/blank/.

The server lists them (list-blank-tapes, CassetteService.ListBlankTapes) and
copies one into place on request (create-tape, CassetteService.CreateTape). A
template's file name is its label plus ".uef", so adding a length is adding a
line to TEMPLATES.

Each is a UEF 0.10 holding an &0000 origin chunk reading "Beebium" and one
&0116 gap of the tape's length in seconds, exact in float32. There is no date
and no other chunk, so the files are byte-reproducible.

    make_blank_tapes.py [OUT_DIRPATH]   build (default: the committed tapes/blank/)
    make_blank_tapes.py --check         build into a temporary directory; fail
                                        unless every file is byte-identical to the
                                        committed one and the set matches
"""

from __future__ import annotations

import struct
import sys
import tempfile
from pathlib import Path

COMMITTED_DIRPATH = Path(__file__).resolve().parent.parent / "tapes" / "blank"

# Each template: its label (the file name without ".uef") and its length in seconds.
TEMPLATES = [
    ("Blank C15 (15 minutes)", 900.0),
    ("Blank C30 (30 minutes)", 1800.0),
    ("Blank C60 (60 minutes)", 3600.0),
    ("Blank C90 (90 minutes)", 5400.0),
]


def chunk(chunk_id: int, body: bytes) -> bytes:
    return struct.pack("<HI", chunk_id, len(body)) + body


def blank_tape(seconds: float) -> bytes:
    if struct.unpack("<f", struct.pack("<f", seconds))[0] != seconds:
        raise ValueError(f"{seconds} s is not exact in float32")
    return (b"UEF File!\x00" + bytes([10, 0])
            + chunk(0x0000, b"Beebium\x00")
            + chunk(0x0116, struct.pack("<f", seconds)))


def build(out_dirpath: Path) -> None:
    out_dirpath.mkdir(parents=True, exist_ok=True)
    for label, seconds in TEMPLATES:
        (out_dirpath / f"{label}.uef").write_bytes(blank_tape(seconds))


def check() -> int:
    with tempfile.TemporaryDirectory(prefix="beebium-blank-tapes-") as temporary:
        built_dirpath = Path(temporary)
        build(built_dirpath)
        built = sorted(p.name for p in built_dirpath.iterdir())
        committed = sorted(p.name for p in COMMITTED_DIRPATH.iterdir()) if COMMITTED_DIRPATH.is_dir() else []
        failed = False
        if built != committed:
            print(f"The blank tapes in {COMMITTED_DIRPATH} are not the set this script makes: "
                  f"it makes {built}, and the directory holds {committed}.", file=sys.stderr)
            failed = True
        for name in built:
            committed_filepath = COMMITTED_DIRPATH / name
            if committed_filepath.is_file() and committed_filepath.read_bytes() != (built_dirpath / name).read_bytes():
                print(f"The blank tape {name} differs from what this script makes.", file=sys.stderr)
                failed = True
        if failed:
            print("Run scripts/make_blank_tapes.py to rebuild them.", file=sys.stderr)
            return 1
    print(f"The {len(built)} blank tapes match.")
    return 0


def main(argv: list[str]) -> int:
    if argv[1:] == ["--check"]:
        return check()
    if len(argv) > 2:
        print(__doc__, file=sys.stderr)
        return 2
    build(Path(argv[1]) if len(argv) == 2 else COMMITTED_DIRPATH)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
