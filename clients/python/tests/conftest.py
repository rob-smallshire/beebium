# Copyright 2025 Robert Smallshire <robert@smallshire.org.uk>
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

"""pytest configuration for beebium Python client tests.

The beebium pytest fixtures are auto-registered via the entry point in pyproject.toml.
This conftest.py can add additional test-specific fixtures if needed.
"""

# The beebium fixtures (bbc, bbc_shared, stopped_bbc, etc.) are automatically
# available from the beebium.pytest_plugin module via the entry point.

# Add any test-specific fixtures here if needed.
