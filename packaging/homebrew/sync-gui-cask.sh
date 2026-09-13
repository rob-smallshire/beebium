#!/usr/bin/env bash
#
# Mirror the canonical beebium-gui cask into the Homebrew tap, pinning it to a
# published release's per-architecture DMG assets.
#
# Usage:
#   packaging/homebrew/sync-gui-cask.sh <version> [tap-checkout-dirpath]
#
# Example:
#   packaging/homebrew/sync-gui-cask.sh 0.2.0 ~/Code/homebrew-beebium
#
# It downloads BOTH signed+notarized DMG assets for v<version>
# (Beebium-<version>-macos-{arm64,x86_64}.dmg), computes their sha256s, writes the
# cask with the real urls implied by the version + both hashes into the tap's
# Casks/ directory, and leaves it staged for the maintainer to commit and push.
#
# TRANSITION: releases cut BEFORE the signed-DMG build carry no DMG assets. When
# the DMGs are absent this script is a clean NO-OP (exit 0 with a clear message),
# so syncing an old version -- or the first post-cask release train -- never
# fails. Only releases that actually ship the DMGs get a cask.
#
# Like the Scoop manifest (and unlike the source-tarball formula), the cask
# points at RELEASE ASSETS, so the GitHub Release for v<version> must be
# PUBLISHED -- a draft's asset URLs 404.
set -euo pipefail

here_dirpath="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cask_filepath="${here_dirpath}/beebium-gui.rb"

version="${1:-}"
cask_dirpath="${2:-}"
if [[ -z "${version}" ]]; then
  echo "usage: $0 <version> [tap-checkout-dirpath]" >&2
  exit 2
fi

base_url="https://github.com/rob-smallshire/beebium/releases/download/v${version}"
url_arm="${base_url}/Beebium-${version}-macos-arm64.dmg"
url_intel="${base_url}/Beebium-${version}-macos-x86_64.dmg"

http_code() { curl -s -o /dev/null -w '%{http_code}' -L "$1"; }

# Graceful no-op when the release carries no DMGs (pre-signed-DMG releases).
code_arm="$(http_code "${url_arm}")"
if [[ "${code_arm}" == "404" ]]; then
  echo "No DMG asset at ${url_arm}"
  echo "v${version} predates the signed-DMG build (or has no macOS app); skipping cask sync."
  exit 0
fi
if [[ "${code_arm}" != "200" ]]; then
  echo "unexpected HTTP ${code_arm} for ${url_arm}" >&2
  exit 1
fi

sha_of() {
  local url="$1"
  local tmp
  tmp="$(mktemp -t beebium-gui.XXXXXX.dmg)"
  curl -fsSL "${url}" -o "${tmp}"
  shasum -a 256 "${tmp}" | awk '{print $1}'
  rm -f "${tmp}"
}

echo "Fetching ${url_arm} ..."
sha_arm="$(sha_of "${url_arm}")"
echo "  arm64  sha256 = ${sha_arm}"
echo "Fetching ${url_intel} ..."
sha_intel="$(sha_of "${url_intel}")"
echo "  x86_64 sha256 = ${sha_intel}"

# Produce the pinned cask from the canonical copy: bump the version, and swap the
# two placeholder hashes in place (preserving the arm:/intel: alignment).
pinned="$(
  sed \
    -e "s|^  version .*|  version \"${version}\"|" \
    -e "s|\(arm:[[:space:]]*\)\"[0-9a-f]*\"|\1\"${sha_arm}\"|" \
    -e "s|\(intel:[[:space:]]*\)\"[0-9a-f]*\"|\1\"${sha_intel}\"|" \
    "${cask_filepath}"
)"

if [[ -z "${cask_dirpath}" ]]; then
  echo "----- pinned cask (no tap dir given; printing to stdout) -----"
  echo "${pinned}"
  exit 0
fi

dest_dirpath="${cask_dirpath}/Casks"
mkdir -p "${dest_dirpath}"
dest_filepath="${dest_dirpath}/beebium-gui.rb"
echo "${pinned}" > "${dest_filepath}"
echo "Wrote ${dest_filepath}"
echo "Review, then commit and push the tap manually (or let sync-channels.yml do it)."
