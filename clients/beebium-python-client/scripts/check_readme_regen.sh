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

# Regeneration-is-a-no-op check for the generated README.
#
# Re-renders README.md from readme/README.md.j2 and the snippet files (with the
# locked dev toolchain) and asserts the committed README.md is unchanged. This
# closes the drift hole where the template or a snippet changes but the
# committed README is not regenerated. Paired with the snippet tests (which run
# the examples against the built wheel), it keeps the README both current and
# correct.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CLIENT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$CLIENT_DIR"

echo "Regenerating README.md with the locked toolchain (uv default dev group)..."
uv run python scripts/generate_readme.py

echo "Checking that regeneration was a no-op..."
if ! git diff --quiet -- README.md; then
    echo
    echo "ERROR: regenerating README.md changed the committed file." >&2
    echo "Edit readme/README.md.j2 or a file under readme/snippets/, then run" >&2
    echo "scripts/generate_readme.py and commit the result. Diff:" >&2
    echo
    git --no-pager diff -- README.md >&2
    exit 1
fi

echo "OK: README.md is in sync with its template and snippets."
