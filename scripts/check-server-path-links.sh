#!/usr/bin/env bash
#
# check-server-path-links.sh - assert the .deb / .rpm maintainer scripts put
# every shipped server on PATH, and take every one off again.
#
# The packages lay the server tree under /opt/beebium and symlink each
# /opt/beebium/bin/beebium-model-* into /usr/bin (packaging/debian/postinst,
# packaging/rpm/postinstall); removal deletes those links (prerm, preuninstall).
# Run inside the smoke container, after installing and after removing.
#
# Usage: check-server-path-links.sh installed|removed
#   installed  every /opt/beebium/bin/beebium-model-* server (at least one) has
#              /usr/bin/<name> as a symlink to it, and running the link works
#   removed    no /usr/bin/beebium-model-* entry remains

set -euo pipefail

opt_bin_dirpath="/opt/beebium/bin"
usr_bin_dirpath="/usr/bin"

case "${1:-}" in
    installed)
        server_count=0
        for server_filepath in "${opt_bin_dirpath}"/beebium-model-*; do
            [ -f "${server_filepath}" ] || continue
            name="$(basename "${server_filepath}")"
            link_filepath="${usr_bin_dirpath}/${name}"
            if [ ! -L "${link_filepath}" ]; then
                echo "MISSING /usr/bin link: ${link_filepath} for ${server_filepath}" >&2
                exit 1
            fi
            target="$(readlink "${link_filepath}")"
            if [ "${target}" != "${server_filepath}" ]; then
                echo "WRONG /usr/bin link: ${link_filepath} -> ${target}, expected ${server_filepath}" >&2
                exit 1
            fi
            "${name}" --help >/dev/null || {
                echo "${name} on PATH did not run" >&2
                exit 1
            }
            echo "  ${link_filepath} -> ${target}"
            server_count=$((server_count + 1))
        done
        if [ "${server_count}" -eq 0 ]; then
            echo "NO servers in ${opt_bin_dirpath}" >&2
            exit 1
        fi
        echo "${server_count} servers linked into ${usr_bin_dirpath}"
        ;;
    removed)
        # A glob, not find: the minimal smoke images need not ship findutils.
        # -e misses a dangling link, so test -L as well.
        leftover_count=0
        for leftover_filepath in "${usr_bin_dirpath}"/beebium-model-*; do
            if [ -e "${leftover_filepath}" ] || [ -L "${leftover_filepath}" ]; then
                echo "LEFT BEHIND after removal: ${leftover_filepath}" >&2
                leftover_count=$((leftover_count + 1))
            fi
        done
        [ "${leftover_count}" -eq 0 ] || exit 1
        echo "no beebium-model-* left in ${usr_bin_dirpath}"
        ;;
    *)
        echo "usage: $0 installed|removed" >&2
        exit 2
        ;;
esac
