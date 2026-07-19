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

"""Generate the built-in Acorn glyph set from a MOS ROM image.

The library must not read a ROM at runtime, nor require one to build, so the
glyph data is committed as a C++ source file. This script regenerates that
file, so the same can be done for another MOS version.

    python3 generate_acorn_glyphs.py ../../../roms/acorn-mos_1_20.rom \
        --name acorn-mos-1.20 --output ../src/AcornMos120Glyphs.cpp

The font lives at ROM offset 0x0000 (address &C000), eight bytes per
character, most significant bit leftmost, starting at character 32. Character
c is therefore at (c - 32) * 8. Data continues to character 127; beyond that
the ROM holds 6502 code. See LAST_CHARACTER for where the set stops and why.
"""

import argparse
import hashlib
import pathlib
import re
import sys

FIRST_CHARACTER = 32

# The table continues past 126 -- character 127 has eight bytes of solid
# block -- but 127 is DELETE, which erases a character rather than printing
# one (BBC User Guide 34, "VDU 127 has exactly the same effect"), so those
# bytes are never rendered as a glyph and do not belong in a set that maps
# bitmaps to text.
#
# Including them is also actively harmful: a solid block is exactly the
# complement of a space, so with inverse matching on, every inverse space --
# which is most of an inverse-video line -- would match character 127 and come
# back as an invisible control code instead of a space.
LAST_CHARACTER = 126

BYTES_PER_GLYPH = 8

# Carried into the generated file, which is committed source like any other.
COPYRIGHT_NOTICE = """\
// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. Beebium is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Beebium.
// If not, see <https://www.gnu.org/licenses/>.
"""

# The Acorn character set is ASCII apart from character 96, which is a pound
# sign rather than a grave accent. Verified against the glyph shapes in the
# ROM rather than assumed.
CODEPOINT_OVERRIDES = {
    0x60: (0x00A3, "POUND SIGN"),
}

# Characters whose C++ character literal would be awkward or non-portable.
LITERAL_NAMES = {
    0x20: "SPACE",
}


def codepoint_for(character):
    """Return (codepoint, comment) for a BBC character code."""
    if character in CODEPOINT_OVERRIDES:
        return CODEPOINT_OVERRIDES[character]
    return character, None


def describe(character, codepoint):
    """A 7-bit ASCII comment describing one glyph, for the generated source."""
    if character in LITERAL_NAMES:
        return LITERAL_NAMES[character]
    if character in CODEPOINT_OVERRIDES:
        return CODEPOINT_OVERRIDES[character][1]
    if 0x21 <= character <= 0x7E:
        return "'{}'".format(chr(character))
    return "U+{:04X}".format(codepoint)


def read_font(rom_bytes):
    """Extract (character, rows) pairs from a MOS ROM image."""
    count = LAST_CHARACTER - FIRST_CHARACTER + 1
    needed = count * BYTES_PER_GLYPH
    if len(rom_bytes) < needed:
        raise ValueError(
            "ROM is {} bytes; need at least {} for characters {}-{}".format(
                len(rom_bytes), needed, FIRST_CHARACTER, LAST_CHARACTER
            )
        )
    glyphs = []
    for character in range(FIRST_CHARACTER, LAST_CHARACTER + 1):
        offset = (character - FIRST_CHARACTER) * BYTES_PER_GLYPH
        glyphs.append((character, rom_bytes[offset:offset + BYTES_PER_GLYPH]))
    return glyphs


def identifier_for(set_name):
    """Turn a glyph set name into a C++ identifier fragment."""
    return re.sub(r"[^0-9a-zA-Z]+", "_", set_name).strip("_").lower()


def generate(set_name, rom_filename, rom_bytes, glyphs, output_filename):
    digest = hashlib.sha256(rom_bytes).hexdigest()
    identifier = identifier_for(set_name)

    lines = COPYRIGHT_NOTICE.split("\n") + [
        "// Generated by tools/generate_acorn_glyphs.py. Do not edit by hand.",
        "//",
        "// Source ROM:   {}".format(rom_filename),
        "// SHA-256:      {}".format(digest),
        "// Glyph set:    {}".format(set_name),
        "// Characters:   {}-{} ({} glyphs), 8x8, MSB leftmost".format(
            FIRST_CHARACTER, LAST_CHARACTER, len(glyphs)
        ),
        "//",
        "// Regenerate with:",
        "//     python3 tools/generate_acorn_glyphs.py roms/{} \\".format(
            rom_filename
        ),
        "//         --name {} --output src/{}".format(set_name, output_filename),
        "",
        '#include "BuiltinGlyphSets.hpp"',
        "",
        "namespace screentext::builtin {",
        "",
        "GlyphSet make_{}()".format(identifier),
        "{",
        "    GlyphSet set;",
        '    set.name = "{}";'.format(set_name),
        "    set.glyphs = {",
    ]

    for character, rows in glyphs:
        codepoint, _ = codepoint_for(character)
        body = ", ".join("0x{:02X}".format(byte) for byte in rows)
        lines.append(
            "        Glyph::from_rows(0x{:04X}U, {{{}}}),  // &{:02X} {}".format(
                codepoint, body, character, describe(character, codepoint)
            )
        )

    lines += [
        "    };",
        "    return set;",
        "}",
        "",
        "} // namespace screentext::builtin",
        "",
    ]
    return "\n".join(lines)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom_filepath", type=pathlib.Path, help="MOS ROM image")
    parser.add_argument(
        "--name", default="acorn-mos-1.20", help="name of the generated glyph set"
    )
    parser.add_argument(
        "--output", type=pathlib.Path, required=True, help="C++ file to write"
    )
    args = parser.parse_args(argv)

    rom_bytes = args.rom_filepath.read_bytes()
    glyphs = read_font(rom_bytes)
    source = generate(
        args.name, args.rom_filepath.name, rom_bytes, glyphs, args.output.name
    )

    # UTF-8 rather than ASCII only because of the copyright notice; the glyph
    # data and its comments are 7-bit throughout.
    args.output.write_text(source, encoding="utf-8")
    print(
        "Wrote {} glyphs to {}".format(len(glyphs), args.output), file=sys.stderr
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
