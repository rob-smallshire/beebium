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

"""Unit tests for the README-snippet server-origin guard.

This file is NOT a rendered snippet (it has no readme:begin/end region); it
proves the guard's decision logic directly, without needing a server, so it is
marked no_server to run even when the snippets themselves skip.
"""

import pytest

from _snippet_guard import ACCEPTED_ORIGINS, skip_reason_for_origin

pytestmark = pytest.mark.no_server


@pytest.mark.parametrize("origin", sorted(ACCEPTED_ORIGINS))
def test_accepted_origins_run(origin):
    assert skip_reason_for_origin(origin, f"{origin} (/some/where)") is None


def test_path_origin_is_refused_and_named():
    reason = skip_reason_for_origin("PATH", "PATH (/opt/homebrew/bin/beebium-model-b)")
    assert reason is not None
    assert "PATH" in reason
    assert "/opt/homebrew/bin/beebium-model-b" in reason


def test_unknown_origin_is_refused():
    assert skip_reason_for_origin("something else", "something else (/x)") is not None
