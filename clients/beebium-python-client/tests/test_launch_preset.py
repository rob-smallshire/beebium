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

"""Beebium.launch(preset=...) configures the machine from a built-in preset id."""

from __future__ import annotations

from pathlib import Path

import pytest

from beebium.client import Beebium
from beebium.client.exceptions import ServerNotFoundError


def test_launch_with_preset_id_has_disc_controller(beebium_server_filepath: Path | None) -> None:
    try:
        with Beebium.launch(server=beebium_server_filepath, preset="model-b-disc") as bbc:
            assert bbc.disc.has_controller
    except ServerNotFoundError as e:
        pytest.skip(str(e))


def test_launch_without_preset_has_no_disc_controller(beebium_server_filepath: Path | None) -> None:
    # The control: a bare Model B has no disc controller, so the preset is
    # what supplies one above.
    try:
        with Beebium.launch(server=beebium_server_filepath) as bbc:
            assert not bbc.disc.has_controller
    except ServerNotFoundError as e:
        pytest.skip(str(e))
