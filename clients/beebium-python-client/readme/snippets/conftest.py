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

from beebium.client.installation import ServerInstallation


@pytest.fixture(autouse=True)
def _require_server():
    """Skip a snippet when no emulator server can be resolved.

    The snippets launch or connect to a real server. Where none is available
    (e.g. a unit job with no built server and no beebium-server wheel), skip
    with a clear reason rather than failing -- the server-backed integration
    jobs are where the snippets actually execute.
    """
    try:
        ServerInstallation.default().executable_filepath("model-b")
    except Exception as exc:  # noqa: BLE001 -- any resolution failure means "no server here"
        pytest.skip(f"no beebium-server available for README snippets: {exc}")
