#!/usr/bin/env bash
# Emit the content tag for the Linux build-env image
# (ghcr.io/rob-smallshire/beebium-build-env). The tag is a hash of exactly the
# inputs that determine the vcpkg static dependency tree, so it rotates when --
# and only when -- those inputs change:
#
#   - vcpkg.json                      (the dependency manifest + builtin-baseline)
#   - triplets/                       (the static triplet overlays)
#   - docker/linux-bundle/Dockerfile  (pins the vcpkg commit and defines the
#                                       dependency-install stage)
#
# The hash is architecture-independent: the recipe is identical for amd64 and
# arm64, so callers append the arch to the image tag (e.g. "<hash>-arm64") to
# keep the two built images distinct. See docs/packaging.md, "Persistent
# dependency caching -- the build-env image".
#
# Usage: scripts/build-env-tag.sh   ->   prints a 12-hex-char tag

set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repo_root"

# Portable SHA-256 (Linux CI has sha256sum; a macOS dev box has shasum).
sha256() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum
    else
        shasum -a 256
    fi
}

# Hash the manifest and Dockerfile by content+path, then every triplet overlay
# (sorted, so the order find returns them in cannot change the result), and fold
# all of that into a single digest.
tag="$(
    {
        sha256 <vcpkg.json
        sha256 <docker/linux-bundle/Dockerfile
        find triplets -type f | LC_ALL=C sort | while IFS= read -r f; do
            printf '%s\n' "$f"
            sha256 <"$f"
        done
    } | sha256 | cut -c1-12
)"

printf '%s\n' "$tag"
