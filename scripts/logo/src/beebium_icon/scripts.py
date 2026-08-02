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

"""Superscript and subscript markup.

Chemistry is written with raised and lowered numbers - the mass number of a
nuclide, the count of an atom in a formula - so the content strings accept a
small markup for them:

    2^4K            a raised 4, as in two to the fourth
    ^{16}Bb         a raised 16 before the symbol, as a mass number
    (6522)_2        a lowered 2, as in a molecular formula

A caret or underscore claims the character after it, or a braced group.  A
literal caret or underscore is written \\^ or \\_.

The scripts are drawn by scaling and shifting the same face rather than by
asking for Unicode superscript characters, which most faces only carry for a
few digits, or for OpenType script features, which many faces lack entirely.
"""

from __future__ import annotations

from dataclasses import dataclass

NORMAL = "normal"
SUPERSCRIPT = "superscript"
SUBSCRIPT = "subscript"

_MARKS = {"^": SUPERSCRIPT, "_": SUBSCRIPT}


class ScriptMarkupError(ValueError):
    """Raised when script markup is malformed."""


@dataclass(frozen=True)
class Segment:
    """A stretch of text set at one level."""

    text: str
    level: str = NORMAL


def parse(text: str) -> tuple[Segment, ...]:
    """Split marked-up text into segments, merging adjacent ones at one level."""
    segments: list[Segment] = []
    literal: list[str] = []
    index = 0

    def flush() -> None:
        if literal:
            segments.append(Segment("".join(literal)))
            literal.clear()

    while index < len(text):
        character = text[index]
        if character == "\\" and index + 1 < len(text) and text[index + 1] in "\\^_":
            literal.append(text[index + 1])
            index += 2
            continue
        if character not in _MARKS:
            literal.append(character)
            index += 1
            continue

        level = _MARKS[character]
        index += 1
        if index >= len(text):
            raise ScriptMarkupError(
                f"{text!r} ends with {character!r}, which has nothing to raise or lower"
            )
        if text[index] == "{":
            closing = text.find("}", index)
            if closing < 0:
                raise ScriptMarkupError(f"{text!r} has an unclosed {{ after {character!r}")
            scripted = text[index + 1 : closing]
            index = closing + 1
            if not scripted:
                raise ScriptMarkupError(f"{text!r} has an empty group after {character!r}")
        else:
            scripted = text[index]
            index += 1
        flush()
        segments.append(Segment(scripted, level))

    flush()
    return tuple(segments) or (Segment(""),)


def plain(text: str) -> str:
    """The text with its markup removed, for measuring and for error messages."""
    return "".join(segment.text for segment in parse(text))
