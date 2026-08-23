# syntax=docker/dockerfile:1
#
# Self-contained Beebium Linux server bundle.
#
# Builds the server executables with gRPC/protobuf (and abseil, re2, c-ares,
# openssl, zlib, upb) statically linked via vcpkg, installs the relocatable
# /opt-style tree, proves it runs in this build-floor image, and emits a
# distributable tarball.
#
# The build floor is debian:bookworm (glibc 2.36) on purpose: a bundle linked
# against an older glibc runs on any newer-glibc distro, so one artifact covers
# Ubuntu 22.04+, Raspberry Pi OS (Bookworm), and Arch (glibc 2.43). Building
# natively on the target distro would invert that and break portability.
#
# Per-arch build (run arm64 natively on Apple Silicon; amd64 is emulated):
#   docker buildx build --platform linux/arm64 \
#       -f docker/linux-bundle/Dockerfile \
#       --build-arg VCPKG_TRIPLET=arm64-linux-static \
#       --target artifact --output type=local,dest=./_artifacts .

ARG BASE=debian:bookworm
# The builder stage starts FROM this. It defaults to the local `build-env` stage
# below, so a fresh checkout or fork builds the vcpkg dependencies from scratch
# with no registry access. The release workflow overrides it with a prebuilt
# ghcr.io/rob-smallshire/beebium-build-env image, and buildx then prunes the
# local build-env stage as dead code -- skipping the expensive vcpkg install.
# Declared before the first FROM so it is usable in a later FROM. See
# docs/packaging.md, "Persistent dependency caching -- the build-env image".
ARG BUILDENV_IMAGE=build-env

# ---- build-env: toolchain + pinned vcpkg + the static dependency tree --------
# This stage is what gets published as beebium-build-env; it holds everything up
# to and including the vcpkg install, and nothing source-specific.
FROM ${BASE} AS build-env

ENV DEBIAN_FRONTEND=noninteractive
# Required for vcpkg on non-x64 Linux: vcpkg bundles prebuilt cmake/ninja only
# for x64, so on arm64 it must fall back to the system-provided tools.
ENV VCPKG_FORCE_SYSTEM_BINARIES=1

# Toolchain + vcpkg host-tool prerequisites + libuuid (the b2-derived 6502
# shared lib links rt+uuid on Linux; rt is in glibc, uuid needs uuid-dev) +
# libavahi-client-dev (headers only: the discovery library dlopens
# libavahi-client at runtime, so this adds no runtime dependency to the bundle).
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git curl ca-certificates \
        zip unzip tar pkg-config \
        autoconf automake autoconf-archive libtool m4 \
        python3 perl \
        linux-libc-dev uuid-dev libavahi-client-dev \
    && rm -rf /var/lib/apt/lists/*

# vcpkg pinned to the SAME commit the macOS CI uses (lukka/run-vcpkg
# vcpkgGitCommitId, == vcpkg.json builtin-baseline) so the Linux bundle links
# the identical gRPC/protobuf/abseil versions as the macOS build, and so the
# image is reproducible (a HEAD clone would drift to newer ports over time).
ENV VCPKG_ROOT=/opt/vcpkg
ARG VCPKG_COMMIT=77df67cfff9c12ccfdb52284e07c87c75092f723
RUN git clone https://github.com/microsoft/vcpkg "${VCPKG_ROOT}" \
    && git -C "${VCPKG_ROOT}" checkout "${VCPKG_COMMIT}" \
    && "${VCPKG_ROOT}/bootstrap-vcpkg.sh" -disableMetrics

WORKDIR /src
ARG VCPKG_TRIPLET=arm64-linux-static

# Install the vcpkg dependencies FIRST, from just the manifest + triplets, so
# this (expensive) layer is keyed only on vcpkg.json/triplets and is reused
# across source-only changes. The cache mount persists vcpkg's built binary
# archives across builds, so even a manifest change rebuilds quickly. Mirrors
# the CI split: install here, then configure with VCPKG_MANIFEST_INSTALL=OFF.
#
# --host-triplet pins the build-time tooling (protoc, grpc_cpp_plugin) to the
# same static, release-only triplet as the target. The overlay triplet links
# libs statically but the CRT dynamically, so those tools still run; sharing the
# triplet avoids a second, native (debug+release) build of the whole
# gRPC/protobuf/abseil stack just to obtain them -- the dominant cold-build cost.
COPY vcpkg.json /src/vcpkg.json
COPY triplets /src/triplets
RUN --mount=type=cache,target=/root/.cache/vcpkg \
    "${VCPKG_ROOT}/vcpkg" install \
        --triplet "${VCPKG_TRIPLET}" \
        --host-triplet "${VCPKG_TRIPLET}" \
        --overlay-triplets=/src/triplets \
        --x-install-root=/src/vcpkg_installed

# ---- builder: the source build, on top of the dependency environment ---------
# FROM the build-env above by default, or a prebuilt beebium-build-env image when
# the workflow supplies one (then the vcpkg install above never runs). The
# toolchain, /opt/vcpkg and /src/vcpkg_installed come from that image; its ENV
# (VCPKG_ROOT, VCPKG_FORCE_SYSTEM_BINARIES) and WORKDIR /src are inherited, but
# ARGs are not, so VCPKG_TRIPLET is re-declared for the CMake configure below.
FROM ${BUILDENV_IMAGE} AS builder
ARG VCPKG_TRIPLET=arm64-linux-static

# Now bring in the full source and build only the servers + plugins (the
# beebium-servers aggregate skips the ~65 Catch2 test executables).
# BEEBIUM_BUILD_TESTS stays ON purely so the install_smoke CTest is registered.
#
# BUILD_JOBS caps compile parallelism. The server TUs instantiate the full
# Machine<Hardware> template against the static gRPC/protobuf headers and peak
# at ~1.5 GB each, so -j(nproc) on a memory-limited Docker VM (e.g. 8 GB / 10
# CPUs) OOM-kills cc1plus. Keep it well under MemGB/1.5; override for roomier
# hosts. Emulated (amd64-on-arm64) builds carry extra overhead -- lower it
# further there if needed.
COPY . /src
ARG BUILD_JOBS=4
RUN cmake -S . -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" \
        -DVCPKG_INSTALLED_DIR=/src/vcpkg_installed \
        -DVCPKG_OVERLAY_TRIPLETS=/src/triplets \
        -DVCPKG_TARGET_TRIPLET="${VCPKG_TRIPLET}" \
        -DVCPKG_HOST_TRIPLET="${VCPKG_TRIPLET}" \
        -DVCPKG_MANIFEST_INSTALL=OFF \
        -DBEEBIUM_BUILD_TESTS=ON \
    && cmake --build build --target beebium-servers -j"${BUILD_JOBS}"

# Packaging tools, kept in a late layer so adding them does not invalidate the
# cached vcpkg build above. dpkg-dev provides dpkg-shlibdeps, which
# CPACK_DEBIAN_PACKAGE_SHLIBDEPS uses to derive the .deb runtime dependencies;
# rpm provides rpmbuild, which CPack's RPM generator shells out to (building an
# RPM on this Debian floor is supported and keeps a single portable build image).
RUN apt-get update && apt-get install -y --no-install-recommends \
        dpkg-dev fakeroot file rpm \
    && rm -rf /var/lib/apt/lists/*

# Prove the freshly built tree installs and boots in the build-floor image
# (in-tree CTest), validate the *installed* tree directly (self-containment +
# boot), then build the .deb, .rpm and .tar.gz with CPack and collect them. The
# .deb and .rpm both lay a /opt/beebium system tree (symlinking the servers into
# /usr/bin via their maintainer scripts / scriptlets); the .tar.gz is a
# relocatable single directory.
RUN ctest --test-dir build -R install_smoke --output-on-failure \
    && cmake --install build --prefix /stage/opt/beebium \
    && bash scripts/smoke-installed-tree.sh /stage/opt/beebium \
    && ( cd build && cpack -G "DEB;RPM;TGZ" ) \
    && mkdir -p /out \
    && cp build/beebium-server*.tar.gz build/beebium-server*.deb build/beebium-server*.rpm /out/

# Minimal stage whose only content is the packages, for `buildx --output`.
FROM scratch AS artifact
COPY --from=builder /out/ /
