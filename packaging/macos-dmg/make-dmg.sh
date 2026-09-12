#!/usr/bin/env bash
#
# Build a Beebium macOS DMG with dmgbuild, headlessly (no Finder scripting).
#
# ONE script for both the local preview loop and CI. It runs dmgbuild via uvx
# against settings.py (the reviewed Finder-window layout) with the app and the
# chosen background passed as -D defines.
#
# Usage:
#   packaging/macos-dmg/make-dmg.sh <app-path> <version> <arch> <background.tiff> [--preview]
#
#   <app-path>    path to a built Beebium.app
#   <version>     bare version, e.g. 0.1.9 (volume name becomes "Beebium <version>")
#   <arch>        arm64 | x86_64 (the DMG file name carries it)
#   <background>  multi-resolution TIFF (see gen-backgrounds.py / README.md)
#   --preview     name the DMG Beebium-preview-<bg>.dmg and `open` it so the
#                 maintainer can judge the look without CI
#
# Output directory is $OUT_DIR (default: the current directory), so CI gets
# Beebium-<version>-macos-<arch>.dmg beside the workspace exactly as before.
set -euo pipefail

# Pin dmgbuild so the local loop and CI build byte-for-byte the same tool.
DMGBUILD_VERSION="1.6.1"

here_dirpath="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
settings_filepath="${here_dirpath}/settings.py"

if [ "$#" -lt 4 ]; then
  echo "usage: $0 <app-path> <version> <arch> <background.tiff> [--preview]" >&2
  exit 2
fi
app_path="$1"
version="$2"
arch="$3"
background_filepath="$4"
preview=0
[ "${5:-}" = "--preview" ] && preview=1

for p in "${app_path}" "${background_filepath}" "${settings_filepath}"; do
  [ -e "${p}" ] || { echo "error: not found: ${p}" >&2; exit 1; }
done

# Absolute paths: dmgbuild is run from a temp cwd, and the defines must resolve.
app_abspath="$(cd "$(dirname "${app_path}")" && pwd)/$(basename "${app_path}")"
background_abspath="$(cd "$(dirname "${background_filepath}")" && pwd)/$(basename "${background_filepath}")"

out_dirpath="${OUT_DIR:-.}"
mkdir -p "${out_dirpath}"

if [ "${preview}" = 1 ]; then
  bg_tag="$(basename "${background_filepath}")"
  bg_tag="${bg_tag#bg-}"
  bg_tag="${bg_tag%.tiff}"
  dmg_filepath="${out_dirpath}/Beebium-preview-${bg_tag}.dmg"
  # Distinct volume name per candidate, so several previews mounted at once do
  # not collide (Finder would otherwise suffix identical names " 1"/" 2"/...).
  volume_name="Beebium preview ${bg_tag}"
else
  dmg_filepath="${out_dirpath}/Beebium-${version}-macos-${arch}.dmg"
  volume_name="Beebium ${version}"
fi
rm -f "${dmg_filepath}"

echo "Building ${dmg_filepath}"
echo "  app        ${app_abspath}"
echo "  background ${background_abspath}"
uvx --from "dmgbuild==${DMGBUILD_VERSION}" dmgbuild \
  -s "${settings_filepath}" \
  -D "app=${app_abspath}" \
  -D "background=${background_abspath}" \
  "${volume_name}" \
  "${dmg_filepath}"

echo "Built ${dmg_filepath}"
if [ "${preview}" = 1 ]; then
  open "${dmg_filepath}"
fi
