#!/bin/bash
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
#
# Run one pytest test repeatedly on a simulated slow host and count failures,
# keeping the screen of each failure. Measures how often a timing-sensitive
# test fails at a given speed; written for #132, where a plain BREAK with the
# auto-boot link occasionally comes up on the tape filing system (about 1 run
# in 80 at 0.4x).
#
# Usage (from anywhere):
#   tools/slow-host/plain_break_loop.sh [speed] [runs] [test-id] [out-dirpath]
# Defaults: 0.4, 30, the #132 test, and a temporary directory for the screens.

set -u
TOOLS_DIRPATH="$(cd "$(dirname "$0")" && pwd)"
REPO_DIRPATH="$(cd "$TOOLS_DIRPATH/../.." && pwd)"
speed="${1:-0.4}"
runs="${2:-30}"
test_id="${3:-tests/test_autoboot.py::test_auto_boot_link_makes_plain_break_boot}"
out_dirpath="${4:-$(mktemp -d -t plain_break_loop)}"

cd "$REPO_DIRPATH/clients/beebium-python-client"
fails=0
for i in $(seq 1 "$runs"); do
  out=$(SLOW_HOST_SPEED="$speed" PYTHONPATH="$TOOLS_DIRPATH" timeout 900 \
    uv run --group test python -m pytest "$test_id" -q -p slow_host_plugin -s 2>&1)
  if echo "$out" | grep -q " failed"; then
    fails=$((fails + 1))
    screen_filepath="$out_dirpath/run_${i}.screen"
    echo "$out" | grep -E "^Row +[0-9]+:" | grep -v "\[ *\]" > "$screen_filepath"
    echo "run $i FAILED: $(tr '\n' '|' < "$screen_filepath")"
  fi
done
echo "speed=${speed}x: $fails of $runs failed (screens in $out_dirpath)"
