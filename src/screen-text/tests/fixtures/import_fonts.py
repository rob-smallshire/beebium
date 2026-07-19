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

"""Convert period BBC fonts into glyph sets for the test corpus.

A development tool, not part of the library or its build. Nothing in the build
or the test suite runs it; the glyph sets it produces are committed.

The fonts come from J.G.Harston's collection at https://mdfs.net/Apps/Font/,
in Fonts1.zip to Fonts4.zip -- "various BBC fonts", dated 1989 to 1991. They
are held there as streams of `VDU 23, character, b0 ... b7` definitions, ten
bytes per character, which is how a BBC program would install them.

Which fonts are here is not a matter of taste. Sixty-eight of the collection
parse as text fonts; each was rendered to a full screen and read back, and
these are the ones that told us something the others did not. See README.md.

    python3 import_fonts.py --archives-dirpath <directory of unzipped fonts>
"""

import argparse
import pathlib
import sys

VDU_DEFINE_CHARACTER = 23
BYTES_PER_DEFINITION = 10

FIRST_CHARACTER = 32
LAST_CHARACTER = 126

# Character 96 is a pound sign in the Acorn set, and these fonts follow it.
CODEPOINT_OVERRIDES = {0x60: 0x00A3}

# What each font is here to demonstrate. The wording ends up in the generated
# file, so that a reader knows why it was kept without going back to the
# survey that chose it.
FONTS = {
    "Broadway": "A wholly different face, and no two glyphs alike: the control"
                " for the rest, proving a font this far from the ROM's still"
                " reads perfectly.",
    "FeltPen": "'0' and 'O' are one bitmap, and so are 'l' and '|'. The most"
               " common way for a font to be unreadable in principle.",
    "Futura": "'0' and 'O' again, and '5' and 'S' -- a pair no one would think"
              " to look for.",
    "TrekFont": "'(' and '[' are one bitmap, and so are ')' and ']'.",
    "chocolate1": "'I', 'l' and '|' are all one bitmap: three characters a cell"
                  " could equally be, not two.",
}


def parse_font(data):
    """Read a stream of VDU 23 character definitions."""
    glyphs = {}
    offset = 0
    while offset + BYTES_PER_DEFINITION <= len(data):
        if data[offset] != VDU_DEFINE_CHARACTER:
            return None
        character = data[offset + 1]
        glyphs[character] = data[offset + 2:offset + BYTES_PER_DEFINITION]
        offset += BYTES_PER_DEFINITION
    return glyphs if offset == len(data) else None


def codepoint_for(character):
    return CODEPOINT_OVERRIDES.get(character, character)


def collision_groups(glyphs):
    """Characters sharing one bitmap, which no reader can tell apart."""
    by_bitmap = {}
    for character, rows in sorted(glyphs.items()):
        by_bitmap.setdefault(bytes(rows), []).append(character)
    return [group for group in by_bitmap.values() if len(group) > 1]


def render(name, note, source_filename, glyphs):
    groups = collision_groups(glyphs)

    lines = [
        "# {}".format(name),
        "#",
        "# {}".format(note),
        "#",
        "# From https://mdfs.net/Apps/Font/ ({}), a collection of BBC fonts",
        "# dated 1989 to 1991, held there as VDU 23 character definitions.",
        "#",
        "# Characters {} to {} ({} glyphs).".format(
            FIRST_CHARACTER, LAST_CHARACTER, len(glyphs)
        ),
    ]
    lines[4] = lines[4].format(source_filename)

    if groups:
        lines.append("#")
        lines.append("# Characters sharing one bitmap, which nothing can tell")
        lines.append("# apart from the pixels alone:")
        for group in groups:
            spelled = ", ".join("'{}'".format(chr(c)) for c in group)
            lines.append("#     {}".format(spelled))
    lines.append("")

    for character in sorted(glyphs):
        rows = " ".join("0x{:02X}".format(byte) for byte in glyphs[character])
        comment = "'{}'".format(chr(character)) if 0x21 <= character <= 0x7E \
            else "U+{:04X}".format(codepoint_for(character))
        lines.append("0x{:04X} {}  # {}".format(
            codepoint_for(character), rows, comment
        ))

    return "\n".join(lines) + "\n"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--archives-dirpath",
        type=pathlib.Path,
        required=True,
        help="directory holding the unzipped font files, in any arrangement",
    )
    parser.add_argument(
        "--output-dirpath",
        type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parent / "fonts",
        help="where to write the glyph sets",
    )
    args = parser.parse_args(argv)
    args.output_dirpath.mkdir(parents=True, exist_ok=True)

    missing = []
    for name, note in sorted(FONTS.items()):
        found = [p for p in args.archives_dirpath.rglob(name) if p.is_file()]
        if not found:
            missing.append(name)
            continue

        source = found[0]
        glyphs = parse_font(source.read_bytes())
        if glyphs is None:
            print("{}: not a VDU 23 font stream".format(source), file=sys.stderr)
            missing.append(name)
            continue

        printable = {
            c: rows for c, rows in glyphs.items()
            if FIRST_CHARACTER <= c <= LAST_CHARACTER
        }
        target = args.output_dirpath / "{}.glyphs".format(name.lower())
        target.write_text(render(name, note, source.name, printable),
                          encoding="ascii")
        print("{:12s} {:3d} glyphs, {} collision groups -> {}".format(
            name, len(printable), len(collision_groups(printable)), target.name
        ))

    for name in missing:
        print("missing: {}".format(name), file=sys.stderr)
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
