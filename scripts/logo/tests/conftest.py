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

"""Shared fixtures.

Shared asset-path constants live in tests_support.py, not here: importing a
conftest.py by its bare name (`from conftest import ...`) is fragile when
several conftests are collected together.
"""

from __future__ import annotations

import pytest

from beebium_icon.config import IconConfig, load_config

from tests_support import DEFAULT_CONFIG_FILEPATH


@pytest.fixture(scope="session")
def config() -> IconConfig:
    return load_config(DEFAULT_CONFIG_FILEPATH)
