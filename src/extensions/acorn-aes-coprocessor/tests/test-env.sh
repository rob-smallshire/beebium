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
# test-env.sh, sets up what the Python tests read
# version 2.0.0, 20 November 2026.

# Sourced by run-server-tests.sh and tools/run-py.sh, which set `here` to this folder first.
# Puts the current acorn-aes-coprocessor/ in Beebium's tree, one way, and sets what the Python tests read.

AES_SOURCE="$(cd "$here/../.." && pwd)"
. "$AES_SOURCE/tools/paths.sh"
need_beebium
tree="$BEEBIUM_DIR/src/extensions/acorn-aes-coprocessor"
if [ ! -d "$tree" ]; then
    echo "The plugin is not in Beebium's tree: run tools/plugin-build.sh first." >&2
    exit 1
fi
rsync -rlc --delete "$AES_SOURCE/acorn-aes-coprocessor/" "$tree/"
out="$AES_SCRATCH/server-tests"
rm -rf "$out"
mkdir -p "$out"
clang++ -std=c++20 -O1 -I"$BEEBIUM_DIR/src/core/include" -DAES_ROM_FACTS_DIR="\"$tree/tests/rom-facts\"" \
    -o "$out/aes-banner" "$tree/tests/aes-banner.cpp"
if [ -f "$AES_ROM" ]; then shasum "$AES_ROM"; fi
export AES_SOURCE AES_BANNER_TOOL="$out/aes-banner" AES_PLUGIN_DIR="$tree"
export AES_TUBE_LOG_DIR="$BEEBIUM_BUILD/src/extensions/acorn-aes-coprocessor/tube-log"
python_env
