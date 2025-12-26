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

"""String encoding utilities for BBC Micro memory access.

This module provides functions for encoding and decoding strings in formats
commonly used on the BBC Micro:

- C-style null-terminated strings (ASCIIZ)
- Pascal-style length-prefixed strings (max 255 bytes)
- Fixed-width padded strings

These compose with the memory read/write methods:

    from beebium.strings import c_str, pascal_str, padded_str
    from beebium.strings import parse_c_str, parse_pascal_str, parse_padded_str

    # Writing
    bbc.memory.bus.write(0x1000, c_str("HELLO"))
    bbc.memory.bus.write(0x2000, pascal_str("WORLD"))
    bbc.memory.bus.write(0x3000, padded_str("TEST", 8))

    # Reading
    name = parse_c_str(bbc.memory.bus.read(0x1000, 256))
    msg = parse_pascal_str(bbc.memory.bus.read(0x2000, 256))
    field = parse_padded_str(bbc.memory.bus.read(0x3000, 8))
"""

from __future__ import annotations


def c_str(s: str, encoding: str = "ascii") -> bytes:
    """Encode a string as null-terminated bytes (C-style / ASCIIZ).

    Args:
        s: The string to encode.
        encoding: Character encoding (default 'ascii').

    Returns:
        Bytes containing the encoded string followed by a null terminator.
    """
    return s.encode(encoding) + b"\x00"


def parse_c_str(data: bytes, encoding: str = "ascii") -> str:
    """Decode a null-terminated string from bytes.

    Reads up to the first null byte. If no null is found, decodes the
    entire buffer.

    Args:
        data: The bytes to decode.
        encoding: Character encoding (default 'ascii').

    Returns:
        The decoded string (excluding the null terminator).
    """
    null_pos = data.find(b"\x00")
    if null_pos == -1:
        return data.decode(encoding)
    return data[:null_pos].decode(encoding)


def pascal_str(s: str, encoding: str = "ascii") -> bytes:
    """Encode a string as length-prefixed bytes (Pascal-style).

    The result is a single length byte followed by the string bytes.
    Maximum string length is 255 bytes.

    Args:
        s: The string to encode.
        encoding: Character encoding (default 'ascii').

    Returns:
        Bytes containing a length byte followed by the encoded string.

    Raises:
        ValueError: If the encoded string exceeds 255 bytes.
    """
    encoded = s.encode(encoding)
    if len(encoded) > 255:
        raise ValueError(
            f"string too long for pascal_str: {len(encoded)} bytes (max 255)"
        )
    return bytes([len(encoded)]) + encoded


def parse_pascal_str(data: bytes, encoding: str = "ascii") -> str:
    """Decode a length-prefixed string from bytes.

    Reads the length from the first byte, then decodes that many
    subsequent bytes.

    Args:
        data: The bytes to decode (must have at least 1 byte).
        encoding: Character encoding (default 'ascii').

    Returns:
        The decoded string.

    Raises:
        IndexError: If data is empty.
        ValueError: If data is shorter than the indicated length.
    """
    if len(data) == 0:
        raise IndexError("cannot parse pascal_str from empty data")
    length = data[0]
    if len(data) < 1 + length:
        raise ValueError(
            f"data too short: need {1 + length} bytes, got {len(data)}"
        )
    return data[1 : 1 + length].decode(encoding)


def padded_str(s: str, width: int, pad: str = " ", encoding: str = "ascii") -> bytes:
    """Encode a string as fixed-width padded bytes.

    The string is right-padded to the specified width.

    Args:
        s: The string to encode.
        width: The fixed width in bytes.
        pad: The padding character (default space).
        encoding: Character encoding (default 'ascii').

    Returns:
        Bytes containing the encoded string padded to the specified width.

    Raises:
        ValueError: If the encoded string exceeds the specified width.
        ValueError: If pad is not a single character.
    """
    encoded = s.encode(encoding)
    if len(encoded) > width:
        raise ValueError(f"string length {len(encoded)} exceeds width {width}")
    pad_byte = pad.encode(encoding)
    if len(pad_byte) != 1:
        raise ValueError(f"pad must be a single character, got {len(pad_byte)} bytes")
    return encoded.ljust(width, pad_byte)


def parse_padded_str(data: bytes, pad: str = " ", encoding: str = "ascii") -> str:
    """Decode a padded string by stripping trailing pad characters.

    Args:
        data: The bytes to decode.
        pad: The padding character to strip (default space).
        encoding: Character encoding (default 'ascii').

    Returns:
        The decoded string with trailing pad characters removed.
    """
    return data.decode(encoding).rstrip(pad)
