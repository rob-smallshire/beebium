# Copyright 2026 James Sargent <acheton1984@letterboxes.org>
#
# This file is part of AESulator, a plugin for Beebium.
#
# AESulator is free software: you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version. AESulator is distributed in the hope that it will
# be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# You should have received a copy of the GNU General Public License along with AESulator.
# If not, see <https://www.gnu.org/licenses/>.

# AESulator: the Acorn ARM Evaluation System, a Beebium plugin.
# cover_support.py, what the Tube call tests share
# version 2.0.0, 20 November 2026.

"""What test_aes_tube_cover_io.py, test_aes_tube_cover_swis.py and test_aes_tube_cover_break.py share: the
addresses, flags and code lines their programs use, and the helpers. How these tests are
written, and where their expected values come from: test_aes_tube_cover_io.py."""
from pathlib import Path

import dfs_image
from aes_host import conversation

PROGRAM, TEXT, ROUTINE, BLOCK = 0x1000, 0x1100, 0x1040, 0x2000
C_FLAG, V_FLAG, I_FLAG = 1 << 29, 1 << 28, 1 << 27  # DOC, SW p.1.
UNDEFINED = (0xEE000000, "undefined")
OPEN_UPDATE = [
    (0xE3A000C0, "MOV R0,#&C0"),    # Open for update: never buffered (Kernel:705-765)
    (0xE3A01C11, "MOV R1,#&1100"),  # the name
    (0xEF00000D, "SWI &D"),         # Open
    (0xE1A07000, "MOV R7,R0"),      # keep the handle
    (0xE1A01007, "MOV R1,R7"),      # the handle, for what follows
]
OPEN_INPUT = [(0xE3A00040, "MOV R0,#&40"), *OPEN_UPDATE[1:]]  # Input only: buffered
CLOSE_R7 = [
    (0xE3A00000, "MOV R0,#&0"),  # R0 = 0 closes
    (0xE1A01007, "MOV R1,R7"),   # the handle
    (0xEF00000D, "SWI &D"),      # Open
]


def pattern(length):
    return bytes((i * 7 + 3) & 255 for i in range(length))


def disc_with(folder, name, size):
    path = Path(folder) / "scratch.ssd"
    path.write_bytes(dfs_image.make([(name, 0x8000, 0x8000, pattern(size))]))
    return path


def on_disc(disc, name):
    return dfs_image.catalogue(disc.read_bytes())[f"$.{name}"][2]


def words(data):
    return [int.from_bytes(data[i:i + 4], "little") for i in range(0, len(data), 4)]


def calls_of(aes, command):
    return [(call, reply) for call, reply in conversation(aes.wire) if call[0] == command]
