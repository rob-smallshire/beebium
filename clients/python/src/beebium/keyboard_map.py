# Copyright 2025 Robert Smallshire <robert@smallshire.org.uk>
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

"""BBC Micro keyboard matrix mapping.

Maps Unicode characters to BBC keyboard matrix positions (row, column).
Based on the BBC Micro keyboard layout.

BBC Keyboard Matrix Layout (10 columns x 8 rows):
    Row 0: SHIFT(0), CTRL(1), [links 2-9]
    Row 1: Q(0), 3(1), 4(2), 5(3), f4(4), 8(5), f7(6), -(7), ^/~(8), LEFT(9)
    Row 2: f0(0), W(1), E(2), T(3), 7/'(4), I(5), 9(6), 0(7), £/_(8), DOWN(9)
    Row 3: 1/!(0), 2/"(1), D(2), R(3), 6/&(4), U(5), O(6), P(7), [(8), UP(9)
    Row 4: CAPS(0), A(1), X(2), F(3), Y(4), J(5), K(6), @(7), :/* (8), RETURN(9)
    Row 5: SHIFTLOCK(0), S(1), C(2), G(3), H(4), N(5), L(6), ;/+(7), ](8), DELETE(9)
    Row 6: TAB(0), Z(1), SPACE(2), V(3), B(4), M(5), ,/<(6), ./>(/7), //?(/8), COPY(9)
    Row 7: ESC(0), f1(1), f2(2), f3(3), f5(4), f6(5), f8(6), f9(7), \\(8), RIGHT(9)
"""

from __future__ import annotations

# Special key positions
SHIFT_KEY = (0, 0)
CTRL_KEY = (0, 1)
RETURN_KEY = (4, 9)
DELETE_KEY = (5, 9)
ESCAPE_KEY = (7, 0)
SPACE_KEY = (6, 2)

# Character to (row, column, needs_shift) mapping
_CHARACTER_MAP: dict[str, tuple[int, int, bool]] = {
    # Letters (lowercase - no shift)
    "a": (4, 1, False),
    "b": (6, 4, False),
    "c": (5, 2, False),
    "d": (3, 2, False),
    "e": (2, 2, False),
    "f": (4, 3, False),
    "g": (5, 3, False),
    "h": (5, 4, False),
    "i": (2, 5, False),
    "j": (4, 5, False),
    "k": (4, 6, False),
    "l": (5, 6, False),
    "m": (6, 5, False),
    "n": (5, 5, False),
    "o": (3, 6, False),
    "p": (3, 7, False),
    "q": (1, 0, False),
    "r": (3, 3, False),
    "s": (5, 1, False),
    "t": (2, 3, False),
    "u": (3, 5, False),
    "v": (6, 3, False),
    "w": (2, 1, False),
    "x": (4, 2, False),
    "y": (4, 4, False),
    "z": (6, 1, False),
    # Letters (uppercase - need shift)
    "A": (4, 1, True),
    "B": (6, 4, True),
    "C": (5, 2, True),
    "D": (3, 2, True),
    "E": (2, 2, True),
    "F": (4, 3, True),
    "G": (5, 3, True),
    "H": (5, 4, True),
    "I": (2, 5, True),
    "J": (4, 5, True),
    "K": (4, 6, True),
    "L": (5, 6, True),
    "M": (6, 5, True),
    "N": (5, 5, True),
    "O": (3, 6, True),
    "P": (3, 7, True),
    "Q": (1, 0, True),
    "R": (3, 3, True),
    "S": (5, 1, True),
    "T": (2, 3, True),
    "U": (3, 5, True),
    "V": (6, 3, True),
    "W": (2, 1, True),
    "X": (4, 2, True),
    "Y": (4, 4, True),
    "Z": (6, 1, True),
    # Digits (unshifted)
    "0": (2, 7, False),
    "1": (3, 0, False),
    "2": (3, 1, False),
    "3": (1, 1, False),
    "4": (1, 2, False),
    "5": (1, 3, False),
    "6": (3, 4, False),
    "7": (2, 4, False),
    "8": (1, 5, False),
    "9": (2, 6, False),
    # Shifted digits produce symbols
    "!": (3, 0, True),  # Shift+1
    '"': (3, 1, True),  # Shift+2
    "#": (1, 1, True),  # Shift+3
    "$": (1, 2, True),  # Shift+4
    "%": (1, 3, True),  # Shift+5
    "&": (3, 4, True),  # Shift+6
    "'": (2, 4, True),  # Shift+7
    "(": (1, 5, True),  # Shift+8
    ")": (2, 6, True),  # Shift+9
    # Punctuation (unshifted)
    " ": (6, 2, False),  # Space
    ",": (6, 6, False),  # Comma
    ".": (6, 7, False),  # Period
    "/": (6, 8, False),  # Slash
    ";": (5, 7, False),  # Semicolon
    ":": (4, 8, False),  # Colon
    "[": (3, 8, False),  # Left bracket
    "]": (5, 8, False),  # Right bracket
    "-": (1, 7, False),  # Minus/hyphen
    "^": (1, 8, False),  # Caret
    "@": (4, 7, False),  # At sign
    "\\": (7, 8, False),  # Backslash
    "_": (2, 8, False),  # Underscore (unshifted on BBC)
    # Shifted punctuation
    "<": (6, 6, True),  # Shift+,
    ">": (6, 7, True),  # Shift+.
    "?": (6, 8, True),  # Shift+/
    "+": (5, 7, True),  # Shift+;
    "*": (4, 8, True),  # Shift+:
    "{": (3, 8, True),  # Shift+[
    "}": (5, 8, True),  # Shift+]
    "=": (1, 7, True),  # Shift+-
    "~": (1, 8, True),  # Shift+^
    "|": (7, 8, True),  # Shift+\
    # Control characters
    "\r": (4, 9, False),  # Return/Enter
    "\n": (4, 9, False),  # Newline (treat as Return)
}


def char_to_matrix(char: str) -> tuple[int, int, bool] | None:
    """Map a character to BBC keyboard matrix position.

    Args:
        char: A single character to map.

    Returns:
        A tuple of (row, column, needs_shift), or None if unmapped.
    """
    return _CHARACTER_MAP.get(char)


def matrix_to_char(row: int, column: int, shifted: bool = False) -> str | None:
    """Map a matrix position back to a character.

    Args:
        row: The keyboard matrix row (0-7).
        column: The keyboard matrix column (0-9).
        shifted: Whether shift is pressed.

    Returns:
        The character, or None if not a character key.
    """
    for char, (r, c, needs_shift) in _CHARACTER_MAP.items():
        if r == row and c == column and needs_shift == shifted:
            return char
    return None
