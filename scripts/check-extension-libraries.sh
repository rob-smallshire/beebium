#!/usr/bin/env bash
#
# check-extension-libraries.sh - assert every plugin extension in an installed
# or extracted Beebium tree has its library beside its manifest.
#
# The server discovers a plugin from <prefix>/bin/extensions/<name>/manifest.json
# and loads <library><suffix> from the same directory, where <library> is the
# manifest's "library" field and <suffix> is .dylib, .so or .dll (PluginLoader).
# `list-extensions` reads only the manifests, so it lists a plugin whose library
# is missing; this check does not. It needs only bash, sed and find, so it runs
# in the minimal Debian and Arch smoke containers and under Git Bash on Windows.
#
# Usage: check-extension-libraries.sh <prefix> [<suffix>]
#   <prefix>  the tree containing bin/extensions/ (an install prefix, an
#             extracted .tar.gz or ZIP, a wheel's _bundle, a Homebrew libexec)
#   <suffix>  the library suffix; defaults to this platform's (.dylib on macOS,
#             .dll on Windows, .so elsewhere). Pass it to check another
#             platform's tree, e.g. a Windows ZIP extracted on a Mac.

set -euo pipefail

prefix="${1:?usage: check-extension-libraries.sh <prefix> [<suffix>]}"
suffix="${2:-}"
if [ -z "${suffix}" ]; then
    case "$(uname -s)" in
        Darwin) suffix=".dylib" ;;
        MINGW* | MSYS* | CYGWIN* | Windows_NT) suffix=".dll" ;;
        *) suffix=".so" ;;
    esac
fi

extensions_dirpath="${prefix}/bin/extensions"
if [ ! -d "${extensions_dirpath}" ]; then
    echo "EXTENSIONS CHECK FAIL: no extensions directory: ${extensions_dirpath}" >&2
    exit 1
fi

checked=0
missing=0
for manifest_filepath in "${extensions_dirpath}"/*/manifest.json; do
    [ -f "${manifest_filepath}" ] || continue
    checked=$((checked + 1))
    extension_dirpath="$(dirname "${manifest_filepath}")"
    library="$(sed -n 's/.*"library"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "${manifest_filepath}" | head -n 1)"
    if [ -z "${library}" ]; then
        echo "EXTENSIONS CHECK FAIL: no \"library\" in ${manifest_filepath}" >&2
        missing=$((missing + 1))
        continue
    fi
    library_filepath="${extension_dirpath}/${library}${suffix}"
    if [ -f "${library_filepath}" ]; then
        echo "  ok  ${library}${suffix}"
    else
        echo "EXTENSIONS CHECK FAIL: $(basename "${extension_dirpath}"): library missing: ${library_filepath}" >&2
        missing=$((missing + 1))
    fi
done

if [ "${checked}" -eq 0 ]; then
    echo "EXTENSIONS CHECK FAIL: no extension manifests under ${extensions_dirpath}" >&2
    exit 1
fi
if [ "${missing}" -ne 0 ]; then
    echo "EXTENSIONS CHECK FAIL: ${missing} of ${checked} extensions have no ${suffix} library beside the manifest" >&2
    exit 1
fi
echo "extensions check OK: all ${checked} extensions have their ${suffix} library beside the manifest"
