#!/usr/bin/env bash
#
# Rebuild the bundled Level 3 File Server master disc image from component
# parts, producing discs/bundled/l3fs-v1_26b.tar.xz -- the committed artifact
# that the CMake build decompresses and ships (see DiscPaths / deployment.md).
#
#   build-l3fs-image.sh [OUT_TARBALL]   build (default: the committed path)
#   build-l3fs-image.sh --check         build twice; fail unless both tarballs
#                                       are byte-identical and their image is
#                                       byte-identical to the committed one
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
# DETERMINISM: the image and the tarball are byte-reproducible.
#   * oaknut stamps "today" into the AFS partition -- its creation date (in
#     both info sectors) and every file and directory entry -- and has no
#     option to set any of them directly. Every oaknut command therefore runs
#     with the calendar frozen (freezegun) at CREATED, and `set-datestamp -r`
#     then pins every FILE to EPOCH, a fixed era-appropriate constant.
#     set-datestamp cannot reach the partition's creation date or the root's
#     entries for its directories, which keep CREATED. Both are FIXED
#     constants -- never "now" or a release date -- so a rebuild equals the
#     committed image across days and releases. CREATED is the day the
#     committed master was first built, so pinning it reproduces that master
#     exactly rather than revising it (a revised image would need a new name;
#     see deployment.md). The ADFS side is date-free.
#   * oaknut and its dependencies are pinned to exact versions, so a newer
#     release cannot change the bytes it writes.
#   * The tarball is written with numeric owner 0, empty user and group names,
#     mode 0644 and EPOCH as every mtime, so it records nothing about the
#     builder. Its xz stream is reproducible for a given Python and liblzma;
#     the image inside it is reproducible everywhere.
# Verify with `build-l3fs-image.sh --check`.
#
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PARTS="$HERE/l3fs-parts"
COMMITTED_TARBALL="$HERE/../../discs/bundled/l3fs-v1_26b.tar.xz"
IMAGE_ID="l3fs-v1_26b"          # disc-image revision (FS software stays v1.26)
EPOCH="1987-01-01T00:00:00"     # FIXED file datestamp -- L3FS v1.26 era; see note above
EPOCH_SECONDS=536457600         # the same instant, as a Unix time (UTC)
CREATED="2026-09-15T00:00:00"   # FIXED "today" for oaknut; see note above

# The tool set, pinned in full. oaknut-disc's dependencies are listed because
# an unpinned resolve would float to their newest releases.
OAKNUT=(uvx --python 3.13
    --from "oaknut-disc==12.17.1"
    --with "oaknut-adfs==13.1.3"
    --with "oaknut-afs==13.1.3"
    --with "oaknut-basic==13.1.3"
    --with "oaknut-cli==13.1.3"
    --with "oaknut-codecs==13.1.3"
    --with "oaknut-dfs==13.1.3"
    --with "oaknut-discimage==13.1.3"
    --with "oaknut-exception==13.1.3"
    --with "oaknut-extension==13.1.3"
    --with "oaknut-file==13.1.3"
    --with "oaknut-filesystem==13.1.3"
    --with "freezegun==1.5.5"
    --with "python-dateutil==2.9.0.post0"
    python)

# Run an oaknut CLI (disc or basic) in-process with the calendar frozen.
FROZEN_CLI='
import sys
from freezegun import freeze_time
tool, epoch, args = sys.argv[1], sys.argv[2], sys.argv[3:]
with freeze_time(epoch):
    if tool == "disc":
        from oaknut.disc.cli import cli
    else:
        from oaknut.basic.cli import cli
    cli.main(args=args, prog_name="oaknut-" + tool)
'
oakdisc()  { "${OAKNUT[@]}" -c "$FROZEN_CLI" disc  "$CREATED" "$@"; }
oakbasic() { "${OAKNUT[@]}" -c "$FROZEN_CLI" basic "$CREATED" "$@"; }

# Write <id>.dat + <id>.dsc from WORK into OUT_TARBALL (the names DiscPaths
# expects after decompression), recording nothing about the builder.
write_tarball() {
    local work="$1" out="$2"
    "${OAKNUT[@]}" -c '
import lzma, sys, tarfile
work, out, epoch_seconds, *names = sys.argv[1:]
with lzma.open(out, "wb", preset=6) as xz:
    with tarfile.open(fileobj=xz, mode="w", format=tarfile.USTAR_FORMAT) as tar:
        for name in names:
            info = tar.gettarinfo(f"{work}/{name}", arcname=name)
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            info.mode = 0o644
            info.mtime = int(epoch_seconds)
            with open(f"{work}/{name}", "rb") as f:
                tar.addfile(info, f)
' "$work" "$out" "$EPOCH_SECONDS" "$IMAGE_ID.dat" "$IMAGE_ID.dsc"
}

build() {
    local out_tarball="$1"
    local work
    work="$(mktemp -d)"
    # shellcheck disable=SC2064  # expand $work now, for this call's directory
    trap "rm -rf '$work'" RETURN
    local dat="$work/$IMAGE_ID.dat"

    # 1. ADFS envelope (10 MB hard disc).
    oakdisc create "$dat" --geometry capacity=10MB --title Server

    # 2. The file-server binary (load/exec come from its .inf sidecar).
    oakdisc put "$dat:\$.FS3v126" "$PARTS/FS3v126"

    # 3. Our unattended-start program. Tokenise the source, then emplace it
    #    with BASIC load/exec (CHAIN loads it to PAGE; the addresses are
    #    conventional).
    oakbasic tokenise "$PARTS/StartFS.bas" "$work/StartFS"
    oakdisc put "$dat:\$.StartFS" "$work/StartFS" --load 0xFFFF1900 --exec 0xFFFF8023

    # 4. !BOOT chains StartFS. Deliberately NOT "*ADFS" then CHAIN: selecting
    #    a filing system mid-*EXEC aborts the EXEC stream, so the CHAIN would
    #    never run. ADFS is already the boot filing system (it ran !BOOT), so a
    #    bare CHAIN loads StartFS from it. Then set the disc's boot option to
    #    *EXEC so a booted machine runs !BOOT.
    printf 'CHAIN"StartFS"\r' | oakdisc put "$dat:\$.!BOOT" -
    oakdisc opt "$dat" EXEC

    # 5. The AFS (Level 3) data partition: users + the shipped Library trees.
    oakdisc afs init "$dat" \
        --disc-name L3DATA \
        --user ChrisC:2MB \
        --user HermannH:2MB \
        --omit-user Welcome \
        --emplace Library \
        --emplace Library1

    # 5b. Populate the two user directories with a few short, period-flavoured
    #     text files so the file server isn't a set of empty homes. Sources
    #     live under l3fs-parts/ as ordinary LF text (reviewable); convert
    #     LF -> CR on the way in, the Acorn text convention that *TYPE expects
    #     (cf. !BOOT above). Load/exec stay the 0xFFFF "not meaningful"
    #     sentinel (put's stdin default). Each file carries its own note that
    #     it is fictitious.
    local spec dest src
    for spec in \
        "ChrisC.NOTES:ChrisC-NOTES.txt" \
        "ChrisC.REPLY:ChrisC-REPLY.txt" \
        "HermannH.THINGS:HermannH-THINGS.txt" \
        "HermannH.MEMO:HermannH-MEMO.txt"; do
        dest="${spec%%:*}"
        src="${spec##*:}"
        tr '\n' '\r' < "$PARTS/$src" | oakdisc put "$dat:afs:\$.$dest" -
        # Public read so any logged-in station can *TYPE them, not just the
        # owner or Syst -- the memo exchange spans both users' directories.
        oakdisc chmod "$dat:afs:\$.$dest" WR/R
    done

    # 6. Pin the AFS file datestamps to the fixed epoch (see DETERMINISM
    #    above). This runs AFTER 5b so the user files are pinned too.
    oakdisc set-datestamp "$dat:afs:\$" "$EPOCH" -r

    # 7. Package.
    mkdir -p "$(dirname "$out_tarball")"
    write_tarball "$work" "$out_tarball"
    echo "wrote $out_tarball ($(wc -c < "$out_tarball") bytes)"
}

# Build twice and compare: the two tarballs must be byte-identical, and the
# image inside them byte-identical to the committed tarball's.
check() {
    local check_dirpath
    check_dirpath="$(mktemp -d)"
    # shellcheck disable=SC2064
    trap "rm -rf '$check_dirpath'" EXIT
    build "$check_dirpath/first.tar.xz"
    build "$check_dirpath/second.tar.xz"
    cmp "$check_dirpath/first.tar.xz" "$check_dirpath/second.tar.xz"
    local name
    for name in built committed; do
        mkdir "$check_dirpath/$name"
    done
    tar -xJf "$check_dirpath/first.tar.xz" -C "$check_dirpath/built"
    tar -xJf "$COMMITTED_TARBALL" -C "$check_dirpath/committed"
    for name in "$IMAGE_ID.dat" "$IMAGE_ID.dsc"; do
        cmp "$check_dirpath/built/$name" "$check_dirpath/committed/$name"
    done
    echo "reproducible: two builds are byte-identical, and match the committed image"
}

if [ "${1:-}" = "--check" ]; then
    check
else
    build "${1:-$COMMITTED_TARBALL}"
fi
