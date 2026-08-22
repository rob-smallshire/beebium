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

from beebium.client.discovery import (  # noqa: E402
    _advertisable_ipv4_addresses,
    multicast_loopback_available,
)


def test_advertisable_addresses_exclude_loopback_and_link_local() -> None:
    """The probe advertises where a real mDNS advertiser would: routable IPv4
    only. Advertising over loopback would let a host whose loopback multicast
    works but whose LAN interface has no route report a false positive -- the
    exact macOS-runner failure. IPv6 addresses (tuples from ifaddr) and
    duplicates are dropped."""
    candidates = [
        "127.0.0.1",  # loopback -- excluded
        "169.254.1.2",  # link-local -- excluded
        "192.168.1.39",  # routable -- kept
        "10.0.0.5",  # routable -- kept
        "192.168.1.39",  # duplicate -- kept once
        ("fe80::1", 0, 0),  # IPv6 tuple -- skipped
        "not-an-ip",  # unparseable -- skipped
    ]
    assert _advertisable_ipv4_addresses(candidates) == ["192.168.1.39", "10.0.0.5"]


def test_no_advertisable_address_means_unavailable() -> None:
    """With only loopback there is no interface a real advertiser could use, so
    discovery is reported unavailable."""
    assert _advertisable_ipv4_addresses(["127.0.0.1"]) == []


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
