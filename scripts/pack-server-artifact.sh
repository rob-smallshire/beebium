#!/usr/bin/env bash
#
# pack-server-artifact.sh - pack a CI build tree's server into one tarball for
# upload as a workflow artifact.
#
# actions/upload-artifact follows symlinks, so uploading the beebium-<model>
# names puts a whole copy of the multi-call server in the artifact for each
# model. A tarball keeps them as the relative links they are, and keeps the
# executable bits, which an artifact drops.
#
# The tarball holds, at their paths relative to the build directory:
#   src/server/beebium-server         the multi-call server
#   src/server/beebium-<model>        each model's name, a link to it
#   src/server/{presets,blank-discs,blank-tapes,extensions}/
#   lib/libbeebium_extension_api.{so,dylib}, lib/libbeebium_extension_ui_proto.{so,dylib}
#
# A consumer extracts it into a directory of its own, conventionally build/,
# so the server's build-time RPATH into ../../lib resolves:
#   tar -xzf beebium-server.tar.gz -C build
#
# Usage: pack-server-artifact.sh <build-dirpath> <archive-filepath>

set -euo pipefail

usage="usage: pack-server-artifact.sh <build-dirpath> <archive-filepath>"
build_dirpath="${1:?${usage}}"
archive_filepath="${2:?${usage}}"
build_dirpath="$(cd "${build_dirpath}" && pwd)"
mkdir -p "$(dirname "${archive_filepath}")"
archive_filepath="$(cd "$(dirname "${archive_filepath}")" && pwd)/$(basename "${archive_filepath}")"

fail() { echo "pack-server-artifact.sh: $*" >&2; exit 1; }

cd "${build_dirpath}"
server_relpath="src/server/beebium-server"
[ -f "${server_relpath}" ] && [ ! -L "${server_relpath}" ] && [ -x "${server_relpath}" ] \
    || fail "${build_dirpath}/${server_relpath} is missing, not executable, or a link"

# Each model's name is a link to beebium-server beside it (the server's
# POST_BUILD step makes them). A regular file there would put a whole server in
# the artifact again, so refuse one.
model_relpaths=()
for path in src/server/beebium-*; do
    [ "${path}" = "${server_relpath}" ] && continue
    [ -d "${path}" ] && continue
    case "${path}" in *.dSYM) continue ;; esac
    [ -L "${path}" ] || fail "${build_dirpath}/${path} is not a link to beebium-server"
    [ "$(readlink "${path}")" = "beebium-server" ] \
        || fail "${build_dirpath}/${path} -> $(readlink "${path}"); expected the relative beebium-server"
    model_relpaths+=("${path}")
done
[ "${#model_relpaths[@]}" -gt 0 ] || fail "no beebium-<model> links beside ${server_relpath}"

tree_relpaths=()
for name in presets blank-discs blank-tapes extensions; do
    [ -d "src/server/${name}" ] || fail "${build_dirpath}/src/server/${name}/ is missing"
    tree_relpaths+=("src/server/${name}")
done

library_relpaths=()
for name in libbeebium_extension_api libbeebium_extension_ui_proto; do
    found=""
    for suffix in so dylib; do
        if [ -f "lib/${name}.${suffix}" ]; then
            found="lib/${name}.${suffix}"
        fi
    done
    [ -n "${found}" ] || fail "${build_dirpath}/lib/${name}.so or .dylib is missing"
    library_relpaths+=("${found}")
done

# COPYFILE_DISABLE keeps macOS tar from adding ._ AppleDouble entries.
COPYFILE_DISABLE=1 tar -czf "${archive_filepath}" \
    "${server_relpath}" "${model_relpaths[@]}" "${tree_relpaths[@]}" "${library_relpaths[@]}"
echo "Packed ${server_relpath}, ${#model_relpaths[@]} model links, ${tree_relpaths[*]} and ${library_relpaths[*]}"
echo "into ${archive_filepath} ($(wc -c <"${archive_filepath}" | tr -d ' ') bytes)"
