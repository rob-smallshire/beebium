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

"""Test-support for the README snippets.

These files are the source of truth for the code shown in the generated
README (see scripts/generate_readme.py). They run as real tests against the
installed wheel so the examples cannot rot. This conftest is scaffolding only;
none of it is rendered into the README.
"""

import pytest

from _snippet_guard import snippet_skip_reason


def pytest_configure(config: pytest.Config) -> None:
    config.addinivalue_line(
        "markers",
        "no_server: a snippet-support unit test that runs without a server "
        "(exempt from the server-origin guard).",
    )


@pytest.fixture(autouse=True)
def _require_server(request: pytest.FixtureRequest):
    """Skip a snippet -- with a precise reason -- unless a server the examples are
    about is available.

    Where none is (a unit job with no built server and no beebium-server wheel),
    or only a stray PATH server is, skip rather than fail: the server-backed
    integration jobs set BEEBIUM_SERVER and are where the snippets execute.
    Tests marked ``no_server`` (the guard's own unit tests) are exempt.
    """
    if request.node.get_closest_marker("no_server"):
        return
    reason = snippet_skip_reason()
    if reason:
        pytest.skip(reason)
