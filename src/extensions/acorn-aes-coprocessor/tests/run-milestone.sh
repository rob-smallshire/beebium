#!/bin/sh
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
# run-milestone.sh, builds and runs the milestone
# version 2.0.0, 20 November 2026.

# The milestone run: the Executive from reset to its first Tube access. Give the ROM file.
# Prints the ROM's SHA-1 first, because every result records which ROM it used (USER).

set -e
here="$(cd "$(dirname "$0")" && pwd)"
rom="${1:?usage: run-milestone.sh <rom file>}"
AES_SOURCE="$(cd "$here/../.." && pwd)"
. "$AES_SOURCE/tools/paths.sh"
out="$AES_SCRATCH/plugin-milestone"
rm -rf "$out"
mkdir -p "$out/src/tests"
cp "$here"/../*.cpp "$here"/../*.hpp "$out/src/"
# plugin_entry.cpp needs Beebium's extension API, which only Beebium's build links (SRC, acorn-aes-coprocessor/CMakeLists.txt).
rm "$out/src/plugin_entry.cpp"
cp "$here"/milestone.cpp "$out/src/tests/"
clang++ -std=c++20 -O2 -Wall -Wextra -o "$out/milestone" "$out"/src/tests/milestone.cpp "$out"/src/*.cpp
shasum "$rom"
"$out/milestone" "$rom"
