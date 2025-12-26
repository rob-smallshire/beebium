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

"""Unit tests for the keyboard mapping module."""

from beebium.keyboard_map import (
    CTRL_KEY,
    DELETE_KEY,
    ESCAPE_KEY,
    RETURN_KEY,
    SHIFT_KEY,
    SPACE_KEY,
    char_to_matrix,
    matrix_to_char,
)


class TestCharToMatrix:
    """Tests for char_to_matrix function."""

    def test_lowercase_letters(self):
        """Lowercase letters should not require shift."""
        for char in "abcdefghijklmnopqrstuvwxyz":
            result = char_to_matrix(char)
            assert result is not None, f"Character '{char}' should be mapped"
            row, column, needs_shift = result
            assert not needs_shift, f"Lowercase '{char}' should not need shift"
            assert 0 <= row <= 7, f"Row for '{char}' should be 0-7"
            assert 0 <= column <= 9, f"Column for '{char}' should be 0-9"

    def test_uppercase_letters(self):
        """Uppercase letters should require shift."""
        for char in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
            result = char_to_matrix(char)
            assert result is not None, f"Character '{char}' should be mapped"
            row, column, needs_shift = result
            assert needs_shift, f"Uppercase '{char}' should need shift"

    def test_digits(self):
        """Digits should not require shift."""
        for char in "0123456789":
            result = char_to_matrix(char)
            assert result is not None, f"Digit '{char}' should be mapped"
            row, column, needs_shift = result
            assert not needs_shift, f"Digit '{char}' should not need shift"

    def test_shifted_digit_symbols(self):
        """Shifted digit symbols should require shift."""
        symbols = "!\"#$%&'()"
        for char in symbols:
            result = char_to_matrix(char)
            assert result is not None, f"Symbol '{char}' should be mapped"
            row, column, needs_shift = result
            assert needs_shift, f"Symbol '{char}' should need shift"

    def test_return_key(self):
        """Return key should map to row 4, column 9."""
        result = char_to_matrix("\r")
        assert result is not None
        row, column, needs_shift = result
        assert row == 4
        assert column == 9
        assert not needs_shift

    def test_newline_maps_to_return(self):
        """Newline should map to return key."""
        result = char_to_matrix("\n")
        assert result == char_to_matrix("\r")

    def test_space(self):
        """Space should map to row 6, column 2."""
        result = char_to_matrix(" ")
        assert result is not None
        row, column, needs_shift = result
        assert row == 6
        assert column == 2
        assert not needs_shift

    def test_unmapped_character(self):
        """Unmapped characters should return None."""
        assert char_to_matrix("\x00") is None
        assert char_to_matrix("\x01") is None


class TestMatrixToChar:
    """Tests for matrix_to_char function."""

    def test_round_trip_lowercase(self):
        """matrix_to_char should reverse char_to_matrix for lowercase."""
        for char in "abcdefghijklmnopqrstuvwxyz":
            result = char_to_matrix(char)
            assert result is not None
            row, column, needs_shift = result
            recovered = matrix_to_char(row, column, shifted=False)
            assert recovered == char

    def test_round_trip_uppercase(self):
        """matrix_to_char should reverse char_to_matrix for uppercase."""
        for char in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
            result = char_to_matrix(char)
            assert result is not None
            row, column, needs_shift = result
            recovered = matrix_to_char(row, column, shifted=True)
            assert recovered == char


class TestSpecialKeys:
    """Tests for special key constants."""

    def test_shift_key(self):
        """SHIFT_KEY should be at row 0, column 0."""
        assert SHIFT_KEY == (0, 0)

    def test_ctrl_key(self):
        """CTRL_KEY should be at row 0, column 1."""
        assert CTRL_KEY == (0, 1)

    def test_return_key(self):
        """RETURN_KEY should be at row 4, column 9."""
        assert RETURN_KEY == (4, 9)

    def test_delete_key(self):
        """DELETE_KEY should be at row 5, column 9."""
        assert DELETE_KEY == (5, 9)

    def test_escape_key(self):
        """ESCAPE_KEY should be at row 7, column 0."""
        assert ESCAPE_KEY == (7, 0)

    def test_space_key(self):
        """SPACE_KEY should be at row 6, column 2."""
        assert SPACE_KEY == (6, 2)
