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

"""Unit tests for the string encoding utilities."""

import pytest

from beebium.strings import (
    c_str,
    padded_str,
    parse_c_str,
    parse_padded_str,
    parse_pascal_str,
    pascal_str,
)


class TestCStr:
    """Tests for c_str (null-terminated strings)."""

    def test_encode_simple(self):
        """Encoding adds null terminator."""
        assert c_str("HELLO") == b"HELLO\x00"

    def test_encode_empty(self):
        """Empty string produces just null terminator."""
        assert c_str("") == b"\x00"

    def test_encode_returns_bytes(self):
        """Result is a bytes instance."""
        result = c_str("TEST")
        assert isinstance(result, bytes)

    def test_parse_simple(self):
        """Parsing stops at null terminator."""
        assert parse_c_str(b"HELLO\x00") == "HELLO"

    def test_parse_ignores_trailing_data(self):
        """Data after null terminator is ignored."""
        assert parse_c_str(b"HELLO\x00WORLD\x00") == "HELLO"

    def test_parse_no_null(self):
        """If no null terminator, decodes entire buffer."""
        assert parse_c_str(b"HELLO") == "HELLO"

    def test_parse_empty_string(self):
        """Null at start produces empty string."""
        assert parse_c_str(b"\x00EXTRA") == ""

    def test_parse_empty_buffer(self):
        """Empty buffer produces empty string."""
        assert parse_c_str(b"") == ""

    def test_roundtrip(self):
        """Encode then parse recovers original string."""
        original = "Hello, BBC Micro!"
        encoded = c_str(original)
        decoded = parse_c_str(encoded)
        assert decoded == original


class TestPascalStr:
    """Tests for pascal_str (length-prefixed strings)."""

    def test_encode_simple(self):
        """Encoding adds length prefix."""
        assert pascal_str("HELLO") == b"\x05HELLO"

    def test_encode_empty(self):
        """Empty string produces just zero length byte."""
        assert pascal_str("") == b"\x00"

    def test_encode_max_length(self):
        """255-byte string is allowed."""
        s = "A" * 255
        result = pascal_str(s)
        assert result[0] == 255
        assert len(result) == 256

    def test_encode_too_long_raises(self):
        """String over 255 bytes raises ValueError."""
        s = "A" * 256
        with pytest.raises(ValueError, match="too long"):
            pascal_str(s)

    def test_encode_returns_bytes(self):
        """Result is a bytes instance."""
        result = pascal_str("TEST")
        assert isinstance(result, bytes)

    def test_parse_simple(self):
        """Parsing reads length then string."""
        assert parse_pascal_str(b"\x05HELLO") == "HELLO"

    def test_parse_ignores_trailing_data(self):
        """Data after indicated length is ignored."""
        assert parse_pascal_str(b"\x05HELLOWORLD") == "HELLO"

    def test_parse_empty_string(self):
        """Zero length produces empty string."""
        assert parse_pascal_str(b"\x00EXTRA") == ""

    def test_parse_empty_buffer_raises(self):
        """Empty buffer raises IndexError."""
        with pytest.raises(IndexError):
            parse_pascal_str(b"")

    def test_parse_truncated_data_raises(self):
        """Buffer shorter than indicated length raises ValueError."""
        with pytest.raises(ValueError, match="too short"):
            parse_pascal_str(b"\x05HI")  # Says 5 bytes, only 2 present

    def test_roundtrip(self):
        """Encode then parse recovers original string."""
        original = "Hello, BBC Micro!"
        encoded = pascal_str(original)
        decoded = parse_pascal_str(encoded)
        assert decoded == original


class TestPaddedStr:
    """Tests for padded_str (fixed-width padded strings)."""

    def test_encode_with_space_padding(self):
        """Default padding is spaces."""
        assert padded_str("TEST", 8) == b"TEST    "

    def test_encode_with_null_padding(self):
        """Can specify null padding."""
        assert padded_str("TEST", 8, pad="\x00") == b"TEST\x00\x00\x00\x00"

    def test_encode_exact_width(self):
        """String exactly matching width needs no padding."""
        assert padded_str("TESTTEST", 8) == b"TESTTEST"

    def test_encode_empty(self):
        """Empty string produces all padding."""
        assert padded_str("", 4) == b"    "

    def test_encode_too_long_raises(self):
        """String exceeding width raises ValueError."""
        with pytest.raises(ValueError, match="exceeds width"):
            padded_str("TOOLONG", 4)

    def test_encode_multichar_pad_raises(self):
        """Multi-character pad raises ValueError."""
        with pytest.raises(ValueError, match="single character"):
            padded_str("TEST", 8, pad="XX")

    def test_encode_returns_bytes(self):
        """Result is a bytes instance."""
        result = padded_str("TEST", 8)
        assert isinstance(result, bytes)

    def test_parse_strips_space_padding(self):
        """Default parsing strips trailing spaces."""
        assert parse_padded_str(b"TEST    ") == "TEST"

    def test_parse_strips_null_padding(self):
        """Can strip null padding."""
        assert parse_padded_str(b"TEST\x00\x00\x00\x00", pad="\x00") == "TEST"

    def test_parse_no_padding(self):
        """String without padding parses correctly."""
        assert parse_padded_str(b"TESTTEST") == "TESTTEST"

    def test_parse_all_padding(self):
        """All-padding produces empty string."""
        assert parse_padded_str(b"    ") == ""

    def test_parse_preserves_leading_spaces(self):
        """Only trailing pad characters are stripped."""
        assert parse_padded_str(b"  TEST  ") == "  TEST"

    def test_roundtrip(self):
        """Encode then parse recovers original string."""
        original = "Hello"
        encoded = padded_str(original, 16)
        decoded = parse_padded_str(encoded)
        assert decoded == original

    def test_roundtrip_with_null_padding(self):
        """Roundtrip works with null padding."""
        original = "Hello"
        encoded = padded_str(original, 16, pad="\x00")
        decoded = parse_padded_str(encoded, pad="\x00")
        assert decoded == original
