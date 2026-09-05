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

"""Shared fixtures."""

from __future__ import annotations

from pathlib import Path

import pytest

from beebium_icon.config import IconConfig, load_config

LOGO_DIRPATH = Path(__file__).resolve().parents[1]
CONFIGS_DIRPATH = LOGO_DIRPATH / "configs"
DEFAULT_CONFIG_FILEPATH = CONFIGS_DIRPATH / "beebium.toml"
ARTWORK_FILEPATH = LOGO_DIRPATH / "the-shape-of-beebium.png"


@pytest.fixture(scope="session")
def config() -> IconConfig:
    return load_config(DEFAULT_CONFIG_FILEPATH)
