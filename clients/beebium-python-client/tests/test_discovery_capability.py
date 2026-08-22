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

"""The mDNS capability probe must not give a false positive.

multicast_loopback_available() gates the machine-advertisement tests: they skip
where it reports False. So its negative path -- the browser observes nothing --
must be reliable, otherwise a runner with no multicast route would run those
tests and fail. This exercises that path deterministically by registering
nothing, which is indistinguishable, to the browser, from a route that drops
every packet.
"""

from __future__ import annotations

import pytest

pytest.importorskip("zeroconf", reason="discovery needs the zeroconf extra")

from beebium.client.discovery import multicast_loopback_available  # noqa: E402


def test_reports_unavailable_when_nothing_is_registered() -> None:
    """With no service registered the browser sees nothing, so the probe
    reports False -- the same outcome a no-multicast-route host produces."""
    assert multicast_loopback_available(timeout=1.5, _register=False) is False


def test_reports_available_when_a_service_is_registered() -> None:
    """On a host that can actually do mDNS, registering a service and browsing
    for it on a second instance reports True. Skipped where the host has no
    multicast route, since the positive path cannot be exercised there."""
    if not multicast_loopback_available(timeout=8.0):
        pytest.skip("this host has no usable mDNS multicast route")
    assert multicast_loopback_available(timeout=8.0) is True
