#!/bin/sh
# The Linux client's build dependencies, from the distribution: the one list
# the CI job and the release build both install, as root, in a container of
# each distribution the client is packaged for (Debian 13 trixie, which serves
# Raspberry Pi OS trixie too, and Ubuntu 26.04); the names are the same on
# both. The package's runtime Depends are derived from what these link
# (dpkg-shlibdeps).
set -eu
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
    ca-certificates git cmake ninja-build g++ pkg-config dpkg-dev file \
    qt6-base-dev qt6-base-private-dev qt6-shadertools-dev qt6-multimedia-dev \
    libgrpc++-dev libprotobuf-dev protobuf-compiler protobuf-compiler-grpc libssl-dev \
    nlohmann-json3-dev \
    libxcb1-dev libwayland-dev libxkbcommon-dev libavahi-client-dev

# A container has no machine-id, so it does not identify itself and the
# client, rightly, knows no server to share its filesystem; the tests of that
# sharing then skip. Give a throwaway build container one so they run.
if [ ! -s /etc/machine-id ] && [ -w /etc ]; then
    tr -d '-' < /proc/sys/kernel/random/uuid > /etc/machine-id
fi
