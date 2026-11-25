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
# dfs_image.py, reads and writes DFS disc images
# version 2.0.0, 20 November 2026.

"""A small reader and writer for 200 KB DFS disc images, for the tests' scratch discs.

The format is Acorn's: two catalogue sectors, then files on whole sectors. Only the plain
fields are used. Frozen images are only ever read through a copy (CLAUDE.md).
"""
SECTOR = 256
SECTORS = 800


def make(files, title=b"SCRATCH"):
    """files: a list of (name, load, exec, data). Returns the bytes of a 200 KB image."""
    image = bytearray(SECTORS * SECTOR)
    image[0:8] = title.ljust(8)[:8]
    image[SECTOR + 5] = len(files) * 8
    image[SECTOR + 6] = SECTORS >> 8
    image[SECTOR + 7] = SECTORS & 255
    start = 2
    for index, (name, load, execute, data) in enumerate(files):
        entry = 8 * (index + 1)
        image[entry:entry + 8] = name.encode().ljust(7)[:7] + b"$"
        length = len(data)
        field = SECTOR + entry
        image[field:field + 2] = load.to_bytes(4, "little")[:2]
        image[field + 2:field + 4] = execute.to_bytes(4, "little")[:2]
        image[field + 4:field + 6] = length.to_bytes(4, "little")[:2]
        image[field + 6] = ((execute >> 16 & 3) << 6 | (length >> 16 & 3) << 4
                            | (load >> 16 & 3) << 2 | start >> 8)
        image[field + 7] = start & 255
        image[start * SECTOR:start * SECTOR + length] = data
        start += (length + SECTOR - 1) // SECTOR
    return bytes(image)


def catalogue(image):
    """Returns {name: (load, exec, data)} for the files in directory $ and the others, keyed
    "$.NAME" or "D.NAME"."""
    found = {}
    for entry in range(8, image[SECTOR + 5] + 8, 8):
        name = image[entry:entry + 7].decode("latin-1").rstrip()
        directory = chr(image[entry + 7] & 0x7F)
        field = image[SECTOR + entry:SECTOR + entry + 8]
        high = field[6]
        load = field[0] | field[1] << 8 | (high >> 2 & 3) << 16
        execute = field[2] | field[3] << 8 | (high >> 6 & 3) << 16
        length = field[4] | field[5] << 8 | (high >> 4 & 3) << 16
        start = (high & 3) << 8 | field[7]
        data = image[start * SECTOR:start * SECTOR + length]
        found[f"{directory}.{name}"] = (load, execute, data)
    return found
