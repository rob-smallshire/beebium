#!/usr/bin/env bash
#
# Rebuild the bundled Level 3 File Server master disc image from component
# parts, producing discs/bundled/l3fs-v1_26b.tar.xz -- the committed artifact
# that the CMake build decompresses and ships (see DiscPaths / deployment.md).
#
# This restores the "regenerable from parts, don't commit a raw blob"
# convention while keeping a committed xz for hermetic CI: the parts +
# this script are the source of truth; the xz is what builds consume.
#
# Only TWO parts are committed under l3fs-parts/:
#   * FS3v126 (+ .inf)  -- the Acorn Level 3 File Server v1.26 binary
#   * StartFS.bas       -- our unattended-start BASIC program (see below)
# Everything else is generated: oaknut's --emplace ships the Library and
# Library1 trees, and afs init creates the users and the L3DATA volume.
#
# DETERMINISM: the whole image is byte-reproducible. oaknut has no epoch flag
# on `create`/`afs init`, and afs init stamps the AFS files with *today's*
# date -- so we pin the AFS tree's datestamps to a fixed era-appropriate
# constant (EPOCH below) with `set-datestamp -r` after init. It must be a
# FIXED constant, never "now" or a release date: only a constant keeps rebuild
# == committed-xz across days and releases when the parts are unchanged. The
# ADFS side is already date-free. The final .dat/.dsc mtimes are normalised
# before tarring so the .tar.xz is byte-identical too. Verify with:
#   ./build-l3fs-image.sh /tmp/a.tar.xz && ./build-l3fs-image.sh /tmp/b.tar.xz
#   cmp /tmp/a.tar.xz /tmp/b.tar.xz    # identical
#
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PARTS="$HERE/l3fs-parts"
OUT_TARBALL="${1:-$HERE/../../discs/bundled/l3fs-v1_26b.tar.xz}"
IMAGE_ID="l3fs-v1_26b"          # disc-image revision (FS software stays v1.26)
EPOCH="1987-01-01T00:00:00"     # FIXED datestamp -- L3FS v1.26 era; see note above
MTIME="198701010000"           # same epoch as a touch(1) timestamp
OAKVER="12.17.1"

OAKDISC=(uvx "oaknut-disc[cli]==$OAKVER")
OAKBASIC=(uvx "oaknut-basic[cli]==$OAKVER")

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
DAT="$WORK/$IMAGE_ID.dat"
DSC="$WORK/$IMAGE_ID.dsc"

# 1. ADFS envelope (10 MB hard disc).
"${OAKDISC[@]}" create "$DAT" --geometry capacity=10MB --title Server

# 2. The file-server binary (load/exec come from its .inf sidecar).
"${OAKDISC[@]}" put "$DAT:\$.FS3v126" "$PARTS/FS3v126"

# 3. Our unattended-start program. Tokenise the source, then emplace it with
#    BASIC load/exec (CHAIN loads it to PAGE; the addresses are conventional).
"${OAKBASIC[@]}" tokenise "$PARTS/StartFS.bas" "$WORK/StartFS"
"${OAKDISC[@]}" put "$DAT:\$.StartFS" "$WORK/StartFS" --load 0xFFFF1900 --exec 0xFFFF8023

# 4. !BOOT chains StartFS. Deliberately NOT "*ADFS" then CHAIN: selecting a
#    filing system mid-*EXEC aborts the EXEC stream, so the CHAIN would never
#    run. ADFS is already the boot filing system (it ran !BOOT), so a bare
#    CHAIN loads StartFS from it. Then set the disc's boot option to *EXEC so a
#    booted machine runs !BOOT.
printf 'CHAIN"StartFS"\r' | "${OAKDISC[@]}" put "$DAT:\$.!BOOT" -
"${OAKDISC[@]}" opt "$DAT" EXEC

# 5. The AFS (Level 3) data partition: users + the shipped Library trees.
"${OAKDISC[@]}" afs init "$DAT" \
    --disc-name L3DATA \
    --user ChrisC:2MB \
    --user HermannH:2MB \
    --omit-user Welcome \
    --emplace Library \
    --emplace Library1

# 6. Pin the AFS datestamps to the fixed epoch (see DETERMINISM above).
"${OAKDISC[@]}" set-datestamp "$DAT:afs:\$" "$EPOCH" -r

# 7. Package: normalise the .dat/.dsc mtimes so the tarball is byte-identical
#    across runs, then create <id>.dat + <id>.dsc inside the .tar.xz (the names
#    DiscPaths expects after decompression).
touch -t "$MTIME" "$DAT" "$DSC"
mkdir -p "$(dirname "$OUT_TARBALL")"
( cd "$WORK" && cmake -E tar cJf "$OUT_TARBALL" "$IMAGE_ID.dat" "$IMAGE_ID.dsc" )
echo "wrote $OUT_TARBALL ($(wc -c < "$OUT_TARBALL") bytes)"
