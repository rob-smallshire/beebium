#!/usr/bin/env bash
#
# Rebuild the blank floppy templates in discs/blank/, one empty disc per
# format, which the server lists (list-blank-discs, DiscService.ListBlankDiscs)
# and copies into place (create-disc, DiscService.CreateDisc). A template's
# file name is its label plus the extension the server's format detection
# needs, so adding a format is adding a line below.
#
#   make-blank-discs.sh [OUT_DIRPATH]   build (default: the committed discs/blank/)
#   make-blank-discs.sh --check         build into a temporary directory; fail
#                                       unless every image is byte-identical to
#                                       the committed one and the set matches
#
# The images are byte-reproducible: oaknut and its oaknut-* dependencies are
# pinned to exact versions, so a newer release cannot change the bytes it
# writes, and an empty DFS or ADFS disc carries no date.
#
set -euo pipefail

HERE_DIRPATH="$(cd "$(dirname "$0")" && pwd)"
COMMITTED_DIRPATH="$HERE_DIRPATH/../discs/blank"

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

# Each template: its file name, then the geometry oaknut creates it with.
TEMPLATES=(
    "Acorn DFS 40T SS 100K.ssd|40t-ss"
    "Acorn DFS 80T SS 200K.ssd|80t-ss"
    "Acorn DFS 40T DS 200K.dsd|40t-ds"
    "Acorn DFS 80T DS 400K.dsd|80t-ds"
    "Acorn ADFS S 160K.ads|s"
    "Acorn ADFS M 320K.adm|m"
    "Acorn ADFS L 640K.adl|l"
)

build() {
    local out_dirpath="$1"
    mkdir -p "$out_dirpath"
    local template filename geometry
    for template in "${TEMPLATES[@]}"; do
        filename="${template%%|*}"
        geometry="${template##*|}"
        rm -f "$out_dirpath/$filename"
        "${OAKNUT[@]}" create --geometry "$geometry" "$out_dirpath/$filename"
    done
}

if [[ "${1:-}" == "--check" ]]; then
    work_dirpath="$(mktemp -d)"
    trap 'rm -rf "$work_dirpath"' EXIT
    build "$work_dirpath"
    status=0
    if ! diff -r "$work_dirpath" "$COMMITTED_DIRPATH"; then
        status=1
    fi
    if [[ $status -eq 0 ]]; then
        echo "blank discs reproduce the committed set"
    else
        echo "blank discs differ from the committed set" >&2
    fi
    exit $status
fi

build "${1:-$COMMITTED_DIRPATH}"
