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

"""Build UEF tapes for tests, the way disc tests build their discs.

The MOS cassette block format (sync &2A, header, header CRC, data, data CRC,
CRC-16/XMODEM high byte first) and the MOS's timings (5.1 s of carrier before
the first block, 0.9 s between blocks, 5.1 s after) follow the cassette
research findings, sections 2.3 and 2.4.
"""

from __future__ import annotations

import struct
from pathlib import Path

LEADER_CYCLES = 12240  # 5.1 s of 2400 Hz carrier
BETWEEN_CYCLES = 2160  # 0.9 s


def crc16_xmodem(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def mos_block(name: str, load: int, exec_: int, number: int, data: bytes, flags: int) -> bytes:
    header = name[:10].encode("ascii") + b"\x00"
    header += struct.pack("<IIHHB", load, exec_, number, len(data), flags) + b"\x00" * 4
    block = b"\x2a" + header + crc16_xmodem(header).to_bytes(2, "big")
    if data:
        block += data + crc16_xmodem(data).to_bytes(2, "big")
    return block


def _chunk(chunk_id: int, body: bytes) -> bytes:
    return struct.pack("<HI", chunk_id, len(body)) + body


def _carrier(cycles: int) -> bytes:
    out = b""
    while cycles > 0:
        n = min(cycles, 0xFFFF)
        out += _chunk(0x0110, struct.pack("<H", n))
        cycles -= n
    return out


def uef_of_file(name: str, load: int, exec_: int, data: bytes) -> bytes:
    """One file as MOS 1.20 saves it, as a UEF."""
    out = b"UEF File!\x00\x0a\x00" + _chunk(0x0000, b"beebium tests\x00") + _carrier(LEADER_CYCLES)
    blocks = max(1, (len(data) + 255) // 256)
    for i in range(blocks):
        part = data[i * 256:(i + 1) * 256]
        last = i + 1 == blocks
        out += _chunk(0x0100, mos_block(name, load, exec_, i, part, 0x80 if last else 0x00))
        out += _carrier(LEADER_CYCLES if last else BETWEEN_CYCLES)
    return out


def uef_of_carrier(seconds: float) -> bytes:
    """A tape of nothing but carrier."""
    return b"UEF File!\x00\x0a\x00" + _carrier(int(seconds * 2400))


def write(filepath: Path, data: bytes) -> Path:
    filepath.write_bytes(data)
    return filepath
