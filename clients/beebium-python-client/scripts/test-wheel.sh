#!/bin/bash
# Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
#
# This file is part of Beebium.
#
# Beebium is free software: you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version. Beebium is distributed in the hope that it will
# be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# You should have received a copy of the GNU General Public License along with Beebium.
# If not, see <https://www.gnu.org/licenses/>.

# Build the wheel and run tests against it in a fresh virtualenv.
#
#   scripts/test-wheel.sh                       # the packaging suite (default)
#   scripts/test-wheel.sh tests/ -v --timeout=600   # any pytest arguments
#
# This is the load-bearing packaging gate (see
# docs/discussion/python-client-architecture.md section 4.3): tests must run
# against code installed from the built wheel, never against src/, so that
# missing package data, an unshipped py.typed marker, namespace misconfiguration
# and unregistered entry points are caught rather than silently passed over by a
# source-tree run. CI runs the whole suite this way, including the server-backed
# integration tests (which take the server from BEEBIUM_SERVER as usual).
#
# With no arguments only the server-free packaging assertions in
# tests_packaging/ run, so no emulator server is needed.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CLIENT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$CLIENT_DIR"

# pytest arguments; paths are made absolute because pytest runs from WORK_DIR.
PYTEST_ARGS=()
for arg in "$@"; do
    if [[ -e "$arg" ]]; then
        PYTEST_ARGS+=("$(cd "$(dirname "$arg")" && pwd)/$(basename "$arg")")
    else
        PYTEST_ARGS+=("$arg")
    fi
done
if [[ ${#PYTEST_ARGS[@]} -eq 0 ]]; then
    PYTEST_ARGS=("$CLIENT_DIR/tests_packaging")
fi

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

echo "==> Building wheel"
rm -rf dist
uv build --wheel
# dist/ is emptied above, so exactly one wheel matches; head -n1 is belt-and-braces.
WHEEL="$(ls "$CLIENT_DIR"/dist/*.whl | head -n 1)"
echo "    built $(basename "$WHEEL")"

echo "==> Creating a fresh virtualenv at $WORK_DIR/venv"
# A brand-new venv (NOT the project .venv) so the only thing under test is the
# built wheel and its declared dependencies -- no editable source install can
# shadow it.
uv venv "$WORK_DIR/venv"
VENV_PYTHON="$WORK_DIR/venv/bin/python"
[[ -x "$VENV_PYTHON" ]] || VENV_PYTHON="$WORK_DIR/venv/Scripts/python.exe"  # Windows

echo "==> Installing the built wheel (product) into the fresh venv"
# With the published extras, so tests of the optional features (image export,
# mDNS discovery) run rather than skip.
uv pip install --python "$VENV_PYTHON" "${WHEEL}[imaging,discovery]"

echo "==> Installing the test runner from the 'test' dependency-group"
# Export just the test group (pytest + pytest-timeout; no project, no codegen
# tooling) and install it alongside the wheel. Single source of truth -- the
# runner deps are never duplicated in this script.
uv export --only-group test --no-hashes --no-emit-project -o "$WORK_DIR/test-requirements.txt"
uv pip install --python "$VENV_PYTHON" -r "$WORK_DIR/test-requirements.txt"

echo "==> Running tests against the INSTALLED wheel (cwd outside src/)"
# Invoke the venv's pytest DIRECTLY (not `uv run`, which would sync the project
# environment and reinstall beebium from source, shadowing the wheel). Run from
# WORK_DIR with an absolute test path so `import beebium` resolves from
# site-packages, never from ./src.
cd "$WORK_DIR"
"$VENV_PYTHON" -m pytest "${PYTEST_ARGS[@]}"

echo "==> Tests passed against the installed wheel."
