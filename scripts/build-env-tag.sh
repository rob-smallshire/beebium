#!/usr/bin/env bash
# Emit the content tag for the Linux build-env image
# (ghcr.io/rob-smallshire/beebium-build-env). The tag is a hash of exactly the
# inputs that determine the vcpkg static dependency tree, so it rotates when --
# and only when -- those inputs change:
#
#   - vcpkg.json, EXCLUDING its "version" field (the dependency list and the
#                                       builtin-baseline). bump-my-version rewrites
#                                       vcpkg.json's "version" on every release, but
#                                       that does not change the built dependencies;
#                                       hashing it would rotate the tag on every tag
#                                       and defeat the cache exactly when a release
#                                       needs it. builtin-baseline (the vcpkg commit)
#                                       IS kept -- it determines the ports.
#   - triplets/                       (the static triplet overlays)
#   - the Dockerfile's build-env stage (the text up to the
#                                       `FROM ${BUILDENV_IMAGE}` line -- it pins
#                                       the vcpkg commit and installs the deps).
#                                       The later builder/artifact stages are
#                                       deliberately excluded, so a source-build
#                                       change does not rotate the tag.
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

# The build-env stage is everything before the `FROM ${BUILDENV_IMAGE}` line;
# hashing only it means edits to the later builder/artifact stages do not rotate
# the tag. (If the marker is ever absent, awk prints the whole file, which
# over-invalidates safely rather than under-invalidating.)
buildenv_stage="$(awk '/^FROM \$\{BUILDENV_IMAGE\}/ {exit} {print}' docker/linux-bundle/Dockerfile)"

# The manifest, canonicalised with its "version" field removed so a release-only
# version bump does not rotate the tag (python3 is on every runner, as the
# bump-my-version hooks already rely on).
vcpkg_manifest_for_hash() {
    python3 -c 'import json, sys; d = json.load(sys.stdin); d.pop("version", None); print(json.dumps(d, sort_keys=True))' \
        <vcpkg.json
}

# Hash the manifest (version-stripped) and the build-env stage by content, then
# every triplet overlay (sorted, so the order find returns them in cannot change
# the result), and fold all of that into a single digest.
tag="$(
    {
        vcpkg_manifest_for_hash | sha256
        printf '%s' "$buildenv_stage" | sha256
        find triplets -type f | LC_ALL=C sort | while IFS= read -r f; do
            printf '%s\n' "$f"
            sha256 <"$f"
        done
    } | sha256 | cut -c1-12
)"

printf '%s\n' "$tag"
