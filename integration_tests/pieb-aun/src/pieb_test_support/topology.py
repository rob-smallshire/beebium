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

"""Econet addressing shared by the fixtures and the tests.

Every network number in a PiEconetBridge configuration must be non-zero:
upstream requires it and works out for itself when to present 0 to a given
medium. So the bridge's fileserver lives on net 1, and the bridge knows us by a
non-zero net of our own.

What *we* declare with ``--aun net=`` is a separate matter, and the two need
not agree. Declaring 0 is the historical flat-cloud arrangement. Declaring the
same non-zero net the bridge knows us by is the honest multi-net arrangement --
and is currently broken inbound, which is why it is a test parameter rather
than a constant. See ``docs/discussion/aun-robustness.md``.
"""

BRIDGE_NET = 1
BRIDGE_FS_STATION = 254

# The net the bridge knows us by. Must be non-zero for the bridge's config to
# load at all, whatever we declare on our own side.
BEEBIUM_BRIDGE_SIDE_NET = 2
BEEBIUM_STATION = 1

# What Beebium declares via --aun net=. Zero is the flat-cloud convention and
# is what works today; tests override this via the ``beebium_net`` fixture.
BEEBIUM_NET_DEFAULT = 0

# A net from which the bridge may allocate a station to an AUN source it does
# not recognise. Only used on NAT'd paths -- see the bridge fixture.
DYNAMIC_NET = 2

# The bridge's AUN listener. Real Acorn AUN implementations only ever speak on
# 32768, so the bridge side stays there; Beebium's port is allocated per test.
BRIDGE_AUN_PORT = 32768
