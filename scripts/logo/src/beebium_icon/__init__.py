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

"""Generator for the Beebium application icon.

The icon is a periodic-table tile for the fictional element Beebium (symbol
Bb).  A level-of-detail scheme adds annotations - atomic number, element name,
isotopes, energy levels - as the rendered size grows, so the same design reads
at 16px in a menu and at 1024px in the Finder.
"""

from beebium_icon.config import IconConfig, load_config

__all__ = ["IconConfig", "load_config"]
