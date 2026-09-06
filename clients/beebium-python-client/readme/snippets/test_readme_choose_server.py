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

"""README snippet: choose a machine variant or a specific server."""

import pytest

from _snippet_guard import variant_skip_reason


def test_choose_server():
    # Scaffolding (not rendered): the snippet launches model-b-plus, but a server
    # install may ship only model-b (e.g. the CI integration artifact points
    # BEEBIUM_SERVER at a single-variant build). Skip with a precise reason when
    # the variant is absent rather than fail; the full beebium-server wheel on the
    # verify legs ships all four, so it runs there for real.
    reason = variant_skip_reason("model-b-plus")
    if reason:
        pytest.skip(reason)
    # readme:begin
    from beebium.client import Beebium

    # Pick a machine variant: "model-b" (default), "model-b-plus",
    # "model-b-plus-128k" or "model-b-romram". To run a specific install
    # instead of the default search, pass server="/path/to/beebium" (a binary
    # or an install root), or set the BEEBIUM_SERVER environment variable.
    with Beebium.launch(variant="model-b-plus") as bbc:
        bbc.expect("BASIC")
    # readme:end
