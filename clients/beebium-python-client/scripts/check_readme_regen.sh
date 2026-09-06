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

# Regeneration-is-a-no-op check for the GENERATED READMEs.
#
# Re-renders every generated README from its readme/README.md.j2 + readme/snippets/
# (with the locked dev toolchain) and asserts the committed files are unchanged.
# This closes the drift hole where a template or a snippet changes but the
# committed README is not regenerated. Paired with the snippet tests (which run
# the examples against the built wheels), it keeps the READMEs both current and
# correct.
#
# One generator (scripts/generate_readme.py at the repo root) serves both
# packages; jinja2 comes from this client project's dev group, so this runs
# under `uv run` from the client directory and regenerates the server package's
# README too.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CLIENT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_DIR="$(cd "$CLIENT_DIR/../.." && pwd)"
GENERATOR="$REPO_DIR/scripts/generate_readme.py"

# Every package whose README is generated: <project-dir>:<README path to diff>.
PROJECTS=(
    "$CLIENT_DIR"
    "$REPO_DIR/packaging/python-server"
)

cd "$CLIENT_DIR"

for project in "${PROJECTS[@]}"; do
    echo "Regenerating README for ${project} with the locked toolchain..."
    uv run python "$GENERATOR" "$project"
done

echo "Checking that regeneration was a no-op..."
readmes=()
for project in "${PROJECTS[@]}"; do
    readmes+=("$project/README.md")
done

if ! git -C "$REPO_DIR" diff --quiet -- "${readmes[@]}"; then
    echo
    echo "ERROR: regenerating a README changed the committed file." >&2
    echo "Edit the readme/README.md.j2 or a readme/snippets/ file, then run" >&2
    echo "scripts/generate_readme.py <project-dir> and commit the result. Diff:" >&2
    echo
    git -C "$REPO_DIR" --no-pager diff -- "${readmes[@]}" >&2
    exit 1
fi

echo "OK: all generated READMEs are in sync with their templates and snippets."
