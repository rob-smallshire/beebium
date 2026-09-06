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

"""Test-support for the beebium-server README snippets.

These files are the source of truth for the code shown in the generated
README (see scripts/generate_readme.py at the repo root). They run as real
tests against the installed beebium-server wheel so the examples cannot rot;
the natural home in CI is the verify-server-wheels legs, which install the
wheel. This conftest is scaffolding only; none of it is rendered.
"""

from __future__ import annotations

import pytest


def _server_wheel_reason() -> str | None:
    """Why the snippets should skip, or None when the installed wheel is present.

    The examples need the beebium-server wheel installed (its bundled binaries).
    Where it is absent -- e.g. a bare checkout with no wheel -- skip with a clear
    reason rather than erroring at import time.
    """
    try:
        import beebium.server
    except Exception as exc:  # noqa: BLE001 -- any import failure means "not installed here"
        return f"beebium-server is not installed: {exc}"
    try:
        executable_filepath = beebium.server.executable_filepath("model-b")
    except Exception as exc:  # noqa: BLE001 -- a broken/empty bundle
        return f"beebium-server bundle is incomplete: {exc}"
    if not executable_filepath.exists():
        return f"beebium-server binary is missing: {executable_filepath}"
    return None


@pytest.fixture(autouse=True)
def _require_server_wheel():
    reason = _server_wheel_reason()
    if reason:
        pytest.skip(reason)
