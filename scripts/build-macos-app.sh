#!/usr/bin/env bash
# Build (and optionally run) the macOS frontend for day-to-day development.
#
# The macOS app spans two build systems: CMake builds the headless server
# executables, and the Xcode build produces the app. For development the app is
# NOT self-embedded: it launches servers from the CMake build tree at runtime
# (PresetManager.serversDirpath's dev fallback), so a freshly built server can
# never go stale. This script builds the servers, then builds the app.
#
# The app's default runtime fallback is ~/Code/beebium/build/src/server, so the
# default BUILD_DIR needs no configuration. For a different BUILD_DIR, export
# BEEBIUM_SERVERS_DIRPATH="$BUILD_DIR/src/server" before launching the app.
#
# Producing a DISTRIBUTABLE, self-contained app is a different flow: install a
# vcpkg-static build to a staging prefix and build the app with
# BEEBIUM_SERVERS_BUILD_DIR set to it, so the "Embed Static Server Bundle" phase
# copies the relocatable tree into the bundle. See docs/macos-app-packaging.md.
#
# Usage:
#   scripts/build-macos-app.sh           # build servers + app (Debug)
#   scripts/build-macos-app.sh --run     # ... and launch the app
#   BUILD_DIR=build-release CONFIG=Release scripts/build-macos-app.sh
#
# BUILD_DIR must already be configured with CMake (see README.md).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"
CONFIG="${CONFIG:-Debug}"
MACOS_PROJECT_DIR="$REPO_ROOT/clients/macos/Beebium"
SERVERS_DIR="$BUILD_DIR/src/server"

RUN=0
[ "${1:-}" = "--run" ] && RUN=1

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
  echo "error: $BUILD_DIR is not configured for CMake." >&2
  echo "       Configure it first (see README.md), or set BUILD_DIR=<dir>." >&2
  exit 1
fi

echo "=== Building servers in $BUILD_DIR ==="
cmake --build "$BUILD_DIR" --target beebium-servers --parallel

echo "=== Building macOS app ($CONFIG) ==="
cd "$MACOS_PROJECT_DIR"
xcodebuild build -scheme Beebium -configuration "$CONFIG"

if [ "$RUN" = "1" ]; then
  APP="$(xcodebuild -scheme Beebium -configuration "$CONFIG" -showBuildSettings 2>/dev/null \
    | awk '/ BUILT_PRODUCTS_DIR =/ {print $3}')/Beebium.app"
  echo "=== Launching $APP ==="
  echo "    (servers resolve from $SERVERS_DIR via the runtime dev fallback;"
  echo "     if this is not ~/Code/beebium/build/src/server, export BEEBIUM_SERVERS_DIRPATH first)"
  open "$APP"
fi
