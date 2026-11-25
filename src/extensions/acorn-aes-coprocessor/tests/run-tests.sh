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
# run-tests.sh, builds and runs the plugin's tests
# version 2.0.0, 20 November 2026.

# Builds and runs the plugin's tests with clang++ alone. The source is copied
# one way to the VM's own disk first: the compiler can misread a freshly edited
# file on the shared folder, and the shared folder is slow. The bounds check
# stays on: reading past a register table has no other visible effect. Beebium's Tube chip
# is compiled in from Beebium's folder, as Beebium's own build does, and only linked; so is the
# debugger interface's one source file, for AesDebug.
# Paths: tools/paths.sh. AES_REQUIRE_ALL=1 turns any skip into a failure.

set -e
here="$(cd "$(dirname "$0")" && pwd)"
AES_SOURCE="$(cd "$here/../.." && pwd)"
. "$AES_SOURCE/tools/paths.sh"
need_beebium
out="$AES_SCRATCH/plugin-tests"
rm -rf "$out/src"
mkdir -p "$out/src/tests"
cp "$here"/../*.cpp "$here"/../*.hpp "$out/src/"
# plugin_entry.cpp needs Beebium's extension API, which only Beebium's build links (SRC, acorn-aes-coprocessor/CMakeLists.txt).
rm "$out/src/plugin_entry.cpp"
cp "$here"/*.cpp "$here"/*.hpp "$out/src/tests/"
cp -R "$here/rom-facts" "$out/src/tests/"
mkdir -p "$out/src/beebium"
cp "$here"/../beebium/AesTube.* "$here"/../beebium/AesDebug.* "$here"/../beebium/TubeLog.hpp "$out/src/beebium/"
cp "$here"/../manifest.json "$out/src/"
beebium="$BEEBIUM_DIR/src/core"
clang++ -std=c++20 -O1 -g -Wall -Wextra -fsanitize=bounds -fno-sanitize-recover=bounds $EXTRA_FLAGS \
    -DAES_ROM_FACTS_DIR="\"$out/src/tests/rom-facts\"" \
    -I"$beebium/include" -o "$out/tests" \
    "$out"/src/tests/test_*.cpp "$out"/src/*.cpp "$out"/src/beebium/AesTube.cpp "$out"/src/beebium/AesDebug.cpp \
    "$beebium/src/TubeUla.cpp" "$beebium/extension-api/CpuDebugTarget.cpp"
# The Executive tests read this ROM; every result records which ROM it used (USER).
if [ -f "$AES_ROM" ]; then shasum "$AES_ROM"; fi
AES_PLUGIN_DIR="$out/src" "$out/tests"
# In the shipped folder there are no notes to write the table into.
if [ -d "$AES_SOURCE/scratch" ]; then python3 "$AES_SOURCE/tools/test-table.py"; fi
