#!/usr/bin/env bash
#
# Build the bundled Music 5000 discs from Colin Fraser's original images,
# producing the committed artifacts that the CMake build decompresses into the
# model-b-plus-128k-music-5000 preset (see DiscPaths / deployment.md):
#
#   discs/bundled/hybrid-music-5000-system-4.tar.xz
#       hybrid-music-5000-system-4.ssd   the Music 5000 System Disk, adapted
#       hybrid-ample_1_00.rom            the AMPLE Nucleus 1.00 ROM, from it
#       hybrid-anhf.rom                  the AMPLE Nucleus Help File ROM, from it
#   discs/bundled/amplinex-001-002.tar.xz
#       amplinex-001-002.dsd             Amplinex 001 and 002, unchanged
#
#   build-music5000-discs.sh [OUT_DIRPATH]   build (default: discs/bundled/)
#   build-music5000-discs.sh --check         build twice; fail unless both
#                                            builds are byte-identical and
#                                            match the committed tarballs
#
# The parts are the originals, committed verbatim under music5000-parts/ with
# their provenance (PROVENANCE.md). The system disc is adapted with two
# changes, both made by oaknut-disc so they are reproducible from the original:
#
#   1. $.!BOOT is replaced by a copy of $.runner, AMPLE Studio's own start-up
#      script, without its "M4" INSTALL line. Colin Fraser's page says that
#      with real ROMs fitted the !BOOT can be replaced by runner, and that
#      without a Music 4000 runner can be edited so the M2/M4 modules do not
#      install; the M4 (Music 4000 keyboard) module polls the user port and
#      hangs without the keyboard.
#   2. The boot option is set to 3 (*EXEC), so Shift-Break runs it (the
#      original is 0, so Shift-Break does nothing).
#
# The ROM images are extracted from the disc, not changed.
#
# DETERMINISM: oaknut is pinned to exact versions, the ROM images are copied
# byte for byte, and each tarball is written with numeric owner 0, empty user
# and group names, mode 0644 and a fixed mtime (1986-01-01, the Music 5000's
# year), so it records nothing about the builder. Verify with --check.
#
set -euo pipefail

HERE_DIRPATH="$(cd "$(dirname "$0")" && pwd)"
PARTS_DIRPATH="$HERE_DIRPATH/music5000-parts"
COMMITTED_DIRPATH="$HERE_DIRPATH/../../discs/bundled"
SYSTEM_ID="hybrid-music-5000-system-4"
AMPLINEX_ID="amplinex-001-002"
AMPLE_ROM_FILENAME="hybrid-ample_1_00.rom"
ANHF_ROM_FILENAME="hybrid-anhf.rom"
EPOCH_SECONDS=504921600   # 1986-01-01T00:00:00Z

OAKNUT=(uvx --python 3.13
    --from "oaknut-disc==13.5.0"
    --with "oaknut-adfs==13.5.0"
    --with "oaknut-afs==13.5.0"
    --with "oaknut-basic==13.5.0"
    --with "oaknut-cli==13.5.0"
    --with "oaknut-codecs==13.5.0"
    --with "oaknut-dfs==13.5.0"
    --with "oaknut-discimage==13.5.0"
    --with "oaknut-exception==13.5.0"
    --with "oaknut-extension==13.5.0"
    --with "oaknut-file==13.5.0"
    --with "oaknut-filesystem==13.5.0"
    oaknut-disc)

# Write the named files from WORK_DIRPATH into OUT_FILEPATH, recording nothing
# about the builder.
write_tarball() {
    local work_dirpath="$1" out_filepath="$2"
    shift 2
    python3 -I -c '
import lzma, sys, tarfile
work_dirpath, out_filepath, epoch_seconds, *names = sys.argv[1:]
with lzma.open(out_filepath, "wb", preset=6) as xz:
    with tarfile.open(fileobj=xz, mode="w", format=tarfile.USTAR_FORMAT) as tar:
        for name in names:
            info = tar.gettarinfo(f"{work_dirpath}/{name}", arcname=name)
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            info.mode = 0o644
            info.mtime = int(epoch_seconds)
            with open(f"{work_dirpath}/{name}", "rb") as f:
                tar.addfile(info, f)
' "$work_dirpath" "$out_filepath" "$EPOCH_SECONDS" "$@"
}

build() {
    local out_dirpath="$1"
    local work_dirpath
    work_dirpath="$(mktemp -d)"
    # shellcheck disable=SC2064  # expand now, for this call's directory
    trap "rm -rf '$work_dirpath'" RETURN
    local ssd_filepath="$work_dirpath/$SYSTEM_ID.ssd"

    cp "$PARTS_DIRPATH/M5000-4.ssd" "$ssd_filepath"

    # The ROM images, byte for byte from the original disc.
    "${OAKNUT[@]}" get "$ssd_filepath:R.AMPLE" "$work_dirpath/$AMPLE_ROM_FILENAME" --meta-format none
    "${OAKNUT[@]}" get "$ssd_filepath:\$.ANHF" "$work_dirpath/$ANHF_ROM_FILENAME" --meta-format none

    # !BOOT := runner without the Music 4000 module, run by *EXEC.
    "${OAKNUT[@]}" cat "$ssd_filepath:\$.runner" > "$work_dirpath/runner"
    python3 -I -c '
import sys
data = open(sys.argv[1], "rb").read()
line = b"\"M4\" INSTALL\r"
assert data.count(line) == 1, "runner has no single M4 INSTALL line"
open(sys.argv[2], "wb").write(data.replace(line, b""))
' "$work_dirpath/runner" "$work_dirpath/boot"
    "${OAKNUT[@]}" rm "$ssd_filepath:\$.!BOOT"
    "${OAKNUT[@]}" put "$ssd_filepath:\$.!BOOT" "$work_dirpath/boot" \
        --load 0x00000000 --exec 0x0003FFFF --meta-format none
    "${OAKNUT[@]}" opt "$ssd_filepath" EXEC

    cp "$PARTS_DIRPATH/anex0102.dsd" "$work_dirpath/$AMPLINEX_ID.dsd"

    mkdir -p "$out_dirpath"
    write_tarball "$work_dirpath" "$out_dirpath/$SYSTEM_ID.tar.xz" \
        "$SYSTEM_ID.ssd" "$AMPLE_ROM_FILENAME" "$ANHF_ROM_FILENAME"
    write_tarball "$work_dirpath" "$out_dirpath/$AMPLINEX_ID.tar.xz" "$AMPLINEX_ID.dsd"
    echo "wrote $out_dirpath/$SYSTEM_ID.tar.xz and $out_dirpath/$AMPLINEX_ID.tar.xz"
}

check() {
    local check_dirpath
    check_dirpath="$(mktemp -d)"
    # shellcheck disable=SC2064
    trap "rm -rf '$check_dirpath'" EXIT
    build "$check_dirpath/first"
    build "$check_dirpath/second"
    local id
    for id in "$SYSTEM_ID" "$AMPLINEX_ID"; do
        cmp "$check_dirpath/first/$id.tar.xz" "$check_dirpath/second/$id.tar.xz"
        cmp "$check_dirpath/first/$id.tar.xz" "$COMMITTED_DIRPATH/$id.tar.xz"
    done
    echo "reproducible: two builds are byte-identical, and match the committed tarballs"
}

if [ "${1:-}" = "--check" ]; then
    check
else
    build "${1:-$COMMITTED_DIRPATH}"
fi
