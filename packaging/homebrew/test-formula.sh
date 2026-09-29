#!/usr/bin/env bash
#
# Build, install, test and audit the beebium-server Homebrew formula against the
# current working tree (not a published release). Used both for local validation
# and by the macOS packaging CI, so the two run identical steps.
#
# It packages the working tree into a GitHub-style source tarball, pins a
# throwaway copy of the canonical formula at that tarball, installs it into a
# local tap with --build-from-source, then runs `brew test` and
# `brew audit --strict`, and finally runs every server through the throwaway
# keg's own bin/ symlinks (never whatever `beebium-model-b` resolves to on PATH).
#
# The throwaway formula uses the real `beebium-server` token and is uninstalled
# on exit, so the script refuses to run when a beebium-server keg is already
# installed from any tap. Set BEEBIUM_FORMULA_TEST_REPLACE=1 to have it
# uninstall the existing keg(s) first instead.
#
# Usage: packaging/homebrew/test-formula.sh
set -euo pipefail

here_dirpath="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_dirpath="$(cd "${here_dirpath}/../.." && pwd)"
formula_filepath="${here_dirpath}/beebium-server.rb"

version="$(grep -E '^current_version' "${repo_dirpath}/.bumpversion.toml" \
  | head -1 | sed -E 's/.*"([^"]+)".*/\1/')"
echo "Validating beebium-server formula for version ${version}"

# Refuse to replace an existing install unless explicitly asked.
if existing="$(brew list --formula --versions beebium-server 2>/dev/null)" \
   && [ -n "${existing}" ]; then
  if [ "${BEEBIUM_FORMULA_TEST_REPLACE:-}" != "1" ]; then
    echo "beebium-server is already installed (${existing}):" >&2
    for keg_dirpath in "$(brew --cellar beebium-server)"/*; do
      echo "  ${keg_dirpath}" >&2
    done
    echo "This script installs and then uninstalls the beebium-server formula," >&2
    echo "which would remove that install. Uninstall it yourself, or rerun with" >&2
    echo "BEEBIUM_FORMULA_TEST_REPLACE=1 to let this script replace it." >&2
    exit 1
  fi
  echo "BEEBIUM_FORMULA_TEST_REPLACE=1: uninstalling beebium-server (${existing})"
  brew uninstall --force beebium-server
fi

work_dirpath="$(mktemp -d -t beebium-formula.XXXXXX)"
trap 'rm -rf "${work_dirpath}"' EXIT

# GitHub-style source tarball of the working tree (tracked files, including any
# uncommitted edits via `git stash create`), rooted at beebium-<version>/.
tree_ish="$(cd "${repo_dirpath}" && git stash create || true)"
tree_ish="${tree_ish:-HEAD}"
tarball_filepath="${work_dirpath}/beebium-${version}.tar.gz"
git -C "${repo_dirpath}" archive --format=tar.gz \
  --prefix="beebium-${version}/" -o "${tarball_filepath}" "${tree_ish}"
sha256="$(shasum -a 256 "${tarball_filepath}" | awk '{print $1}')"

# Pin a throwaway formula at the local tarball.
tap_repo_dirpath="$(brew --repository)/Library/Taps/beebium/homebrew-formula-test"
mkdir -p "${tap_repo_dirpath}/Formula"
sed \
  -e "s|^  url .*|  url \"file://${tarball_filepath}\"|" \
  -e "s|^  sha256 .*|  sha256 \"${sha256}\"|" \
  "${formula_filepath}" > "${tap_repo_dirpath}/Formula/beebium-server.rb"

installed_by_script=0
cleanup() {
  if [ "${installed_by_script}" = "1" ]; then
    brew uninstall --force beebium-server >/dev/null 2>&1 || true
  fi
  brew untap beebium/formula-test >/dev/null 2>&1 || true
  rm -rf "${work_dirpath}"
}
trap cleanup EXIT

# CI runner images ship a stale Homebrew formula index. Without this, `brew
# install` pours dependency bottles from the stale index, then `brew test`'s
# developer-mode JSON-API refresh decides those just-installed deps are no longer
# the "latest" and aborts with "missing test dependencies: protobuf grpc". Align
# the index up front so install and test agree. Skipped locally (a dev Mac's
# index is already current, and `brew update` there is slow and noisy).
if [ "${CI:-}" = "true" ]; then
  echo "::group::brew update"
  brew update
  echo "::endgroup::"
fi

echo "::group::brew install --build-from-source"
installed_by_script=1
brew install --build-from-source beebium/formula-test/beebium-server
echo "::endgroup::"

echo "::group::brew test"
brew test beebium/formula-test/beebium-server
echo "::endgroup::"

echo "::group::brew audit --strict"
brew audit --strict beebium/formula-test/beebium-server
echo "::endgroup::"

# Every server variant must be exposed through the keg's bin symlinks (what
# brew links onto PATH), and discovery must resolve plugins out of the
# installed libexec tree. The checks call the keg's own bin/ paths so they
# test this install, not whatever `beebium-model-b` resolves to on PATH.
echo "::group::bin symlink + discovery check"
keg_bin_dirpath="$(brew --prefix beebium/formula-test/beebium-server)/bin"
server_count=0
for server_filepath in "${keg_bin_dirpath}"/beebium-model-*; do
  [ -L "${server_filepath}" ] || {
    echo "NOT A SYMLINK: ${server_filepath}" >&2
    exit 1
  }
  server_ext_out="$("${server_filepath}" list-extensions)"
  echo "${server_ext_out}" | grep -q "/libexec/bin/extensions" || {
    echo "PLUGINS NOT RESOLVED from the keg: ${server_filepath}" >&2
    exit 1
  }
  echo "  $(basename "${server_filepath}"): extensions resolved from the keg"
  server_count=$((server_count + 1))
done
[ "${server_count}" -gt 0 ] || {
  echo "NO beebium-model-* symlinks in ${keg_bin_dirpath}" >&2
  exit 1
}
echo "${server_count} servers exposed in ${keg_bin_dirpath}"

ext_out="$("${keg_bin_dirpath}/beebium-model-b" list-extensions)"
for cli in host-serial aun scsi-hdd ip232-serial rfc2217-client-serial \
           rfc2217-server-serial rpc-serial loopback-serial piconet \
           acorn-scsi acorn-rtc; do
  echo "${ext_out}" | grep -q "^${cli}	" || {
    echo "MISSING extension: ${cli}" >&2
    exit 1
  }
done
echo "all expected extensions discovered"

# ROMs, presets and bundled discs are found relative to the binary's real
# location too; both checks fail if the symlink is not resolved first.
preset_out="$("${keg_bin_dirpath}/beebium-model-b" list-presets)"
for preset in model-b model-b-disc; do
  echo "${preset_out}" | grep -qE "^ +${preset} " || {
    echo "MISSING preset: ${preset}" >&2
    exit 1
  }
done
echo "built-in presets discovered"

shot_filepath="${work_dirpath}/shot.png"
"${keg_bin_dirpath}/beebium-model-b" capture-screenshot --output "${shot_filepath}" --duration 0.5
[ -s "${shot_filepath}" ] || {
  echo "MISSING screenshot: ROM discovery through the bin symlink failed" >&2
  exit 1
}
echo "ROMs discovered (screenshot captured)"
echo "::endgroup::"

echo "Formula validation PASSED."
