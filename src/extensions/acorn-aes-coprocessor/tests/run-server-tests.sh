#!/bin/sh
# Copyright 2026 James Sargent <acheton1984@letterboxes.org>
#
# This file is part of AESulator, a plugin for Beebium.
#
# AESulator is free software: you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version. AESulator is distributed in the hope that it will
# be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# You should have received a copy of the GNU General Public License along with AESulator.
# If not, see <https://www.gnu.org/licenses/>.

# AESulator: the Acorn ARM Evaluation System, a Beebium plugin.
# run-server-tests.sh, runs the tests against the built server
# version 2.0.0, 20 November 2026.

# The AES in the built server, and the in-process machine test. Names the server, never
# discovers one (notes/beebium-baseline.md). Build first with tools/plugin-build.sh. Run in
# Beebium's tree with uv, as its scenario suites are (SRC, its ci.yml). Each skip prints why.
# Paths: tools/paths.sh. AES_REQUIRE_ALL=1 turns any skip into a failure.

set -e
here="$(cd "$(dirname "$0")" && pwd)"
# The paths first, since aes-paths.sh may put uv on PATH; then uv, before anything runs.
AES_SOURCE="$(cd "$here/../.." && pwd)"
. "$AES_SOURCE/tools/paths.sh"
if ! command -v uv >/dev/null 2>&1; then
    echo "The server tests need uv, which is not installed. uv fetches the right Python and the test packages." >&2
    echo "Install it with Homebrew: brew install uv" >&2
    echo "Other ways: https://docs.astral.sh/uv/getting-started/installation/" >&2
    exit 1
fi
. "$here/test-env.sh"
# Catch2 exits 4 when every test skipped (seen 2 Nov 2026).
"$BEEBIUM_BUILD/src/extensions/acorn-aes-coprocessor/test_aes_machine" --success || [ $? = 4 ] || machine=failed
cd "$tree/tests"
uv run --frozen pytest test_server.py test_tube_wire.py test_tube_cover_io.py test_tube_cover_swis.py test_tube_cover_break.py test_portability.py test_tube_log.py test_debugger.py test_tick_guard.py \
    -p no:cacheprovider --timeout=300 -q -rs "$@"
[ -z "$machine" ]
