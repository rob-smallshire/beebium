#!/bin/sh
# Configure, build, test and package the Linux client, from the repository
# root, with its dependencies installed (install-build-deps.sh). The tests run
# without a display (each sets QT_QPA_PLATFORM=offscreen) and without sound.
# The optional distribution suffix goes on the package version
# (0.16.0~debian13); without it the version is plain.
# Usage: clients/linux/ci/build-package.sh [build-dirpath] [jobs] [distribution]
set -eu
build_dirpath=${1:-build-linux-client}
jobs=${2:-2}
distribution=${3:-}
cmake -S clients/linux -B "$build_dirpath" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DBEEBIUM_CLIENT_PACKAGE_DISTRIBUTION="$distribution"
cmake --build "$build_dirpath" --parallel "$jobs"
ctest --test-dir "$build_dirpath" --output-on-failure --parallel "$jobs"
(cd "$build_dirpath" && cpack)
ls "$build_dirpath"/beebium-client_*.deb
