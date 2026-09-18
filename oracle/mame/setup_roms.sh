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
# Assemble a MAME rompath for the BBC B oracle from ROM images the developer
# ALREADY HAS. It never downloads anything and never writes inside the repository.
# ROMs are copyrighted and are not distributed with Beebium; this only copies
# images you already possess into the layout MAME expects, verifying each by SHA-1.
#
# Usage:
#   setup_roms.sh [source ...]
# Each `source` is a directory to search recursively, or a romset .zip to look
# inside. Extra sources also come from BEEBIUM_ROM_SOURCES (colon-separated).
# Defaults searched in addition: the repo's roms/, ~/Code/b-em, ~/Code/beebem-mac.
# Destination: BEEBIUM_MAME_ROMPATH (or ROMPATH), default
#   $XDG_DATA_HOME/beebium/mame-roms  -- always OUTSIDE the repository.
#
# Builds the minimal default `bbcb` set (Intel 8271, DNFS 1.20). Exit 0 if the
# rompath verifies, 3 if anything required is still missing.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
ROMPATH="${BEEBIUM_MAME_ROMPATH:-${ROMPATH:-${XDG_DATA_HOME:-$HOME/.local/share}/beebium/mame-roms}}"

# Refuse to write inside the repository (protects against committing ROMs).
case "$(cd "$ROMPATH" 2>/dev/null && pwd || echo "$ROMPATH")" in
  "$REPO"|"$REPO"/*) echo "refusing: ROMPATH ($ROMPATH) is inside the repo; ROMs must live outside it." >&2; exit 2;;
esac

sha1of() { shasum -a 1 "$1" 2>/dev/null | awk '{print $1}'; }

# Required files for the default bbcb (8271 / DNFS 1.20), pipe-delimited
# "romname|sha1|setdir" (romname may contain spaces/commas). setdir is the MAME
# set the file belongs to (machine bbcb, or a device shortname). Values are from
# `mame -listroms bbcb` (MAME v0.289).
REQUIRED=(
  "os12.rom|0d9bcaf6a393c9ce2359ed700ddb53c232c2c45d|bbcb"
  "basic2.rom|4a7393f3a45ea309f744441c16723e2ef447a281|bbcb"
  "cm62024.bin|b369809275cb67dfd8a749265e91adb2d2558ae6|bbcb"
  "saa5050|6c8daba70374e5aa3a6402f24cdc5f8677d58a0f|saa5050"
  "dnfs120.rom|7e3c536baeae84d6498a14e8405319e01ee78232|bbc_acorn8271"
)
# Optional: the WD1770 like-for-like (bbc_acorn1770, Acorn DFS 2.23). MAME also
# ships DFS 2.26 (dfs v2.26,acorn.rom, sha1 cf2ebc422a8d24ec6f1a0320520c38a0e704109a),
# an exact match to Beebium's WD1770 DFS if you want a byte-for-byte comparison.
OPTIONAL=(
  "dfs v2.23,acorn.rom|0d7ed0b0b3852cb61970ada1993244f2896896aa|bbc_acorn1770"
)

SOURCES=("$@")
IFS=':' read -r -a envsrc <<< "${BEEBIUM_ROM_SOURCES:-}"
SOURCES+=("${envsrc[@]}")
SOURCES+=("$REPO/roms" "$HOME/Code/b-em" "$HOME/Code/beebem-mac")

# Build a SHA-1 index of all candidate files ONCE (portable to bash 3.2: a temp
# file of "sha1<TAB>descriptor" where descriptor is a path or "zip<TAB>member").
# Only ROM-sized files are indexed (<=32k) to keep it quick over large trees;
# zips found anywhere (given directly or inside a searched directory) are opened
# and their members indexed too.
INDEX="$(mktemp)"; trap 'rm -f "$INDEX"' EXIT
echo "Indexing sources by SHA-1 (once)..."
index_zip() {
  local zip="$1" m h
  while IFS= read -r m; do
    [[ -n "$m" ]] || continue
    h="$(unzip -p "$zip" "$m" 2>/dev/null | shasum -a 1 | awk '{print $1}')"
    [[ -n "$h" ]] && printf '%s\tzip\t%s\t%s\n' "$h" "$zip" "$m" >> "$INDEX"
  done < <(unzip -Z1 "$zip" 2>/dev/null)
}
for src in "${SOURCES[@]}"; do
  [[ -n "$src" ]] || continue
  if [[ -f "$src" && "$src" == *.zip ]]; then
    index_zip "$src"
  elif [[ -d "$src" ]]; then
    while IFS= read -r f; do
      if [[ "$f" == *.zip ]]; then index_zip "$f"
      else printf '%s\tfile\t%s\n' "$(sha1of "$f")" "$f" >> "$INDEX"; fi
    done < <(find "$src" -type f \( -size -32k -o -name '*.zip' \) 2>/dev/null)
  fi
done

install_row() {
  local name="$1" sha1="$2" setdir="$3"
  local dest="$ROMPATH/$setdir/$name"
  if [[ -f "$dest" && "$(sha1of "$dest")" == "$sha1" ]]; then
    echo "  ok      $setdir/$name (already present)"; return 0
  fi
  local line kind; line="$(grep -m1 "^$sha1"$'\t' "$INDEX" || true)"
  if [[ -n "$line" ]]; then
    kind="$(printf '%s' "$line" | cut -f2)"
    mkdir -p "$ROMPATH/$setdir"
    if [[ "$kind" == zip ]]; then
      local zip mem; zip="$(printf '%s' "$line" | cut -f3)"; mem="$(printf '%s' "$line" | cut -f4)"
      unzip -p "$zip" "$mem" > "$dest"
    else
      cp "$(printf '%s' "$line" | cut -f3)" "$dest"
    fi
    echo "  copied  $setdir/$name  <- (sha1 $sha1)"; return 0
  fi
  echo "  MISSING $setdir/$name  (need sha1 $sha1)"; return 1
}

# install_set <missing-var-name> <row...>: install each "name|sha1|setdir" row,
# counting misses into the named variable. Avoids bash-4 namerefs for portability.
install_set() {
  local missvar="$1"; shift; local missing=0 row name sha1 setdir
  for row in "$@"; do
    IFS='|' read -r name sha1 setdir <<< "$row"
    install_row "$name" "$sha1" "$setdir" || missing=$((missing+1))
  done
  eval "$missvar=$missing"
}

echo "MAME oracle ROM setup -> $ROMPATH"
echo "Sources: ${SOURCES[*]}"
req_missing=0; opt_missing=0
echo "Required (default bbcb, 8271 / DFS 1.20):"
install_set req_missing "${REQUIRED[@]}"
echo "Optional (bbcb -fdc acorn1770, DFS 2.23):"
install_set opt_missing "${OPTIONAL[@]}"

echo
if command -v mame >/dev/null 2>&1; then
  if mame -rompath "$ROMPATH" -verifyroms bbcb >/tmp/setup_verify.$$ 2>&1; then
    echo "verifyroms bbcb: OK"
  else
    echo "verifyroms bbcb: incomplete (optional Watford BIOS variants may be absent; the default still boots):"
    grep -iE "NOT FOUND|is bad" /tmp/setup_verify.$$ | sed 's/^/  /' || true
  fi
  rm -f /tmp/setup_verify.$$
fi
if (( req_missing )); then
  echo "Still missing $req_missing required file(s); point setup_roms.sh at a directory or romset zip that has them." >&2
  exit 3
fi
echo "Rompath ready. Set BEEBIUM_MAME_ROMPATH=$ROMPATH for mame_run.sh."
