#!/usr/bin/env bash
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
# Reusable MAME BBC oracle wrapper. Headless run of a BBC machine with a disc,
# a keystroke string, a run budget, and a set of memory ranges to dump. MAME is
# an external program (GPL); it is not vendored here, and ROMs are never stored
# in the repository -- see README.md and setup_roms.sh.
#
# Usage:
#   mame_run.sh -m <machine> -d <disc.ssd> [-k <keys>] [-D <sec>] [-R <sec>] \
#               -a "5000:16,00FD:2" [-o out.txt] [-s] [-- <extra mame args>]
#
#   -m  MAME machine. `bbcb` = Intel 8271 (DFS 1.20) default. For the WD1770
#       (DFS 2.23), keep -m bbcb and pass the slot option after `--`:
#         mame_run.sh -m bbcb ... -- -fdc acorn1770
#   -d  floppy image for :flop1 (SSD/DSD/etc)
#   -k  keys to type (\n = RETURN). Typed after -D seconds.
#   -D  integer seconds to wait before typing     (default 3)
#   -R  integer seconds to run after typing        (default 12)
#   -a  comma list of hex ranges addr:len to dump (address in hex, len in decimal)
#   -o  output text file                          (default $OUTDIR/harness_out.txt)
#   -s  also save a PNG snapshot
#   everything after `--` is passed straight to mame.
#
# Env: BEEBIUM_MAME_ROMPATH (or ROMPATH) = directory holding the romsets, OUTSIDE
#      the repository (default $XDG_DATA_HOME/beebium/mame-roms). Build it with
#      setup_roms.sh. OUTDIR = working/output dir, default the current directory.
#
# Exit codes: 0 ok; 2 bad arguments; 3 oracle unavailable (MAME missing or ROMs
# not verified) -- callers can treat 3 as "skip, not a disagreement".
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROMPATH="${BEEBIUM_MAME_ROMPATH:-${ROMPATH:-${XDG_DATA_HOME:-$HOME/.local/share}/beebium/mame-roms}}"
OUTDIR="${OUTDIR:-$PWD}"
MACHINE=""; DISC=""; KEYS=""; DELAY=3; RUN=12; DUMPS=""; OUT="$OUTDIR/harness_out.txt"; SNAP=""
EXTRA=()
while [[ $# -gt 0 ]]; do case "$1" in
  -m) MACHINE="$2"; shift 2;;
  -d) DISC="$2"; shift 2;;
  -k) KEYS="$2"; shift 2;;
  -D) DELAY="$2"; shift 2;;
  -R) RUN="$2"; shift 2;;
  -a) DUMPS="$2"; shift 2;;
  -o) OUT="$2"; shift 2;;
  -s) SNAP=1; shift;;
  --) shift; EXTRA=("$@"); break;;
  *) echo "unknown arg: $1" >&2; exit 2;;
esac; done
[[ -n "$MACHINE" ]] || { echo "need -m machine" >&2; exit 2; }

# Pre-flight: MAME present, and its romset for this machine verifies. A caller
# must be able to tell "oracle not set up" (exit 3) from "oracle ran" (exit 0).
if ! command -v mame >/dev/null 2>&1; then
  echo "mame-oracle: 'mame' not on PATH. Install it (e.g. 'brew install mame'), see README.md." >&2
  exit 3
fi
# A short headless boot is the reliable "can this machine actually run?" check:
# MAME only fatals ("Required files are missing") when a REQUIRED rom is absent,
# not when optional BIOS variants are (those still boot), unlike -verifyroms which
# fails on any missing set.
preflight="/tmp/mame_preflight.$$"
mame "$MACHINE" -rompath "$ROMPATH" -video none -sound none -nothrottle \
  -seconds_to_run 1 -skip_gameinfo >"$preflight" 2>&1 || true
if grep -qiE "Required files are missing|romset .* not found|Fatal error" "$preflight"; then
  echo "mame-oracle: machine '$MACHINE' cannot run under ROMPATH=$ROMPATH:" >&2
  grep -iE "NOT FOUND|missing|Fatal" "$preflight" | sed 's/^/  /' >&2 || true
  echo "  Assemble a rompath with oracle/mame/setup_roms.sh; see README.md. (ROMs are never committed.)" >&2
  rm -f "$preflight"
  exit 3
fi
rm -f "$preflight"

# Total run budget (integer seconds) plus a margin for boot/type.
BUDGET=$(( DELAY + RUN + 4 ))

export HK_KEYS="$KEYS" HK_DELAY="$DELAY" HK_RUN="$RUN" HK_DUMPS="$DUMPS" HK_OUT="$OUT"
[[ -n "$SNAP" ]] && export HK_SNAP=1 || true

args=( "$MACHINE"
  -rompath "$ROMPATH"
  -autoboot_script "$HERE/harness.lua"
  -autoboot_delay 0
  -snapshot_directory "$OUTDIR"
  -video none -sound none -nothrottle -seconds_to_run "$BUDGET"
  -window -nomax -skip_gameinfo )
[[ -n "$DISC" ]] && args+=( -flop1 "$DISC" )
if (( ${#EXTRA[@]} )); then args+=( "${EXTRA[@]}" ); fi

echo "mame ${args[*]}" >&2
exec mame "${args[@]}"
