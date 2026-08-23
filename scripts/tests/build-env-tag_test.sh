#!/usr/bin/env bash
# Regression test for scripts/build-env-tag.sh.
#
# The build-env image tag must NOT rotate on a release-only version bump (that
# would rebuild the ~40-minute dependency image on every tag and make the Linux
# bundles wait for it), but MUST rotate when a real dependency input changes.
#
# Runs the real script against a minimal synthetic tree in a temp dir, so it
# never touches the repository's own vcpkg.json. Run it by hand:
#   bash scripts/tests/build-env-tag_test.sh

set -euo pipefail

script_dirpath="$(cd "$(dirname "$0")" && pwd)"
tag_script="${script_dirpath}/../build-env-tag.sh"

tmp="$(mktemp -d)"
trap 'rm -rf "${tmp}"' EXIT

mkdir -p "${tmp}/scripts" "${tmp}/triplets" "${tmp}/docker/linux-bundle"
cp "${tag_script}" "${tmp}/scripts/build-env-tag.sh"
printf 'set(VCPKG_CRT_LINKAGE dynamic)\n' > "${tmp}/triplets/x64-linux-static.cmake"
cat > "${tmp}/docker/linux-bundle/Dockerfile" <<'DOCKER'
FROM debian:bookworm AS build-env
RUN true
ARG BUILDENV_IMAGE=build-env
FROM ${BUILDENV_IMAGE} AS builder
RUN echo source build
DOCKER

write_manifest() {  # write_manifest <version> <dependency>
    cat > "${tmp}/vcpkg.json" <<JSON
{
  "name": "beebium",
  "version": "$1",
  "builtin-baseline": "77df67cfff9c12ccfdb52284e07c87c75092f723",
  "dependencies": ["$2"]
}
JSON
}

fail() { echo "FAIL: $*" >&2; exit 1; }

write_manifest "0.1.3" "grpc"
tag_v013="$(bash "${tmp}/scripts/build-env-tag.sh")"

# A release-only version bump must leave the tag unchanged.
write_manifest "0.1.4" "grpc"
tag_v014="$(bash "${tmp}/scripts/build-env-tag.sh")"
[ "${tag_v013}" = "${tag_v014}" ] || fail "version bump rotated the tag (${tag_v013} -> ${tag_v014})"

# A real dependency change must rotate the tag.
write_manifest "0.1.4" "grpc-different"
tag_dep="$(bash "${tmp}/scripts/build-env-tag.sh")"
[ "${tag_v014}" != "${tag_dep}" ] || fail "a dependency change did not rotate the tag"

echo "PASS: the build-env tag ignores the version field and tracks dependency changes"
