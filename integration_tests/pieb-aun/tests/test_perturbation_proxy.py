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

"""Tests for the perturbation proxy itself.

A test instrument has to be trustworthy before anything is built on it: a
proxy that silently failed to reorder would make every scenario using it pass
for the wrong reason. These use two plain UDP sockets standing in for Beebium
and the bridge, so they need neither an emulator nor a bridge and run in the
ordinary test suite rather than under ``-m slow``.
"""

from __future__ import annotations

import socket

import pytest

from pieb_test_support.perturb import (
    AUN_HEADER_SIZE,
    AunType,
    Direction,
    PerturbationProxy,
)

RECV_TIMEOUT = 2.0


def aun_datagram(packet_type: int, marker: int) -> bytes:
    """A minimal well-formed AUN datagram, tagged so tests can identify it."""
    header = bytes([packet_type, 0x99, 0x80, 0x00, 0, 0, 0, 0])
    assert len(header) == AUN_HEADER_SIZE
    return header + bytes([marker])


def marker_of(datagram: bytes) -> int:
    return datagram[AUN_HEADER_SIZE]


@pytest.fixture
def endpoints():
    """A stand-in Beebium and bridge, with a proxy between them."""
    bridge = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    bridge.bind(("127.0.0.1", 0))
    bridge.settimeout(RECV_TIMEOUT)

    beebium = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    beebium.bind(("127.0.0.1", 0))
    beebium.settimeout(RECV_TIMEOUT)

    proxy = PerturbationProxy(*bridge.getsockname())
    try:
        yield beebium, bridge, proxy
    finally:
        proxy.stop()
        beebium.close()
        bridge.close()


def send_to_proxy(sock: socket.socket, proxy: PerturbationProxy,
                  datagram: bytes) -> None:
    sock.sendto(datagram, (proxy.address, proxy.port))


def test_relays_outbound(endpoints):
    beebium, bridge, proxy = endpoints

    send_to_proxy(beebium, proxy, aun_datagram(AunType.UNICAST, 1))
    received, sender = bridge.recvfrom(4096)

    assert marker_of(received) == 1
    assert sender == (proxy.address, proxy.port)
    assert proxy.counters.forwarded == 1


def test_relays_inbound_to_the_learned_beebium_address(endpoints):
    beebium, bridge, proxy = endpoints

    # Beebium must speak first: its ephemeral port is not known until it does,
    # so the proxy learns the return address rather than being told it.
    send_to_proxy(beebium, proxy, aun_datagram(AunType.UNICAST, 1))
    _, proxy_address = bridge.recvfrom(4096)

    bridge.sendto(aun_datagram(AunType.ACK, 2), proxy_address)
    back, _ = beebium.recvfrom(4096)
    assert marker_of(back) == 2


def test_holding_an_ack_lets_the_next_datagram_overtake_it(endpoints):
    """The reordering that motivates the whole instrument.

    A fileserver acknowledges a command and then replies as a fresh
    transaction. Held one behind, the reply arrives first -- which is exactly
    what happens on a loaded network, and what used to destroy the reply.
    """
    beebium, bridge, proxy = endpoints

    send_to_proxy(beebium, proxy, aun_datagram(AunType.UNICAST, 0))
    _, proxy_address = bridge.recvfrom(4096)

    proxy.hold_next(Direction.INBOUND, types=(AunType.ACK,), after=1)

    bridge.sendto(aun_datagram(AunType.ACK, 1), proxy_address)
    bridge.sendto(aun_datagram(AunType.UNICAST, 2), proxy_address)

    first, _ = beebium.recvfrom(4096)
    second, _ = beebium.recvfrom(4096)

    assert marker_of(first) == 2, "the reply should have overtaken the ack"
    assert marker_of(second) == 1

    counters = proxy.counters
    assert counters.held == 1
    assert counters.released == 1


def test_a_hold_applies_once_by_default(endpoints):
    beebium, bridge, proxy = endpoints

    send_to_proxy(beebium, proxy, aun_datagram(AunType.UNICAST, 0))
    _, proxy_address = bridge.recvfrom(4096)

    proxy.hold_next(Direction.INBOUND, types=(AunType.ACK,), after=1)

    bridge.sendto(aun_datagram(AunType.ACK, 1), proxy_address)
    bridge.sendto(aun_datagram(AunType.UNICAST, 2), proxy_address)
    beebium.recvfrom(4096)
    beebium.recvfrom(4096)

    # A second ack passes straight through: the rule is spent.
    bridge.sendto(aun_datagram(AunType.ACK, 3), proxy_address)
    third, _ = beebium.recvfrom(4096)
    assert marker_of(third) == 3


def test_dropping_removes_a_datagram_entirely(endpoints):
    beebium, bridge, proxy = endpoints

    send_to_proxy(beebium, proxy, aun_datagram(AunType.UNICAST, 0))
    _, proxy_address = bridge.recvfrom(4096)

    proxy.drop_next(Direction.INBOUND, types=(AunType.ACK,))

    bridge.sendto(aun_datagram(AunType.ACK, 1), proxy_address)
    bridge.sendto(aun_datagram(AunType.UNICAST, 2), proxy_address)

    survivor, _ = beebium.recvfrom(4096)
    assert marker_of(survivor) == 2

    with pytest.raises(socket.timeout):
        beebium.settimeout(0.3)
        beebium.recvfrom(4096)

    assert proxy.counters.dropped == 1


def test_rules_are_type_selective(endpoints):
    """A rule aimed at acks leaves other traffic alone."""
    beebium, bridge, proxy = endpoints

    send_to_proxy(beebium, proxy, aun_datagram(AunType.UNICAST, 0))
    _, proxy_address = bridge.recvfrom(4096)

    proxy.drop_next(Direction.INBOUND, types=(AunType.ACK,))

    bridge.sendto(aun_datagram(AunType.BROADCAST, 1), proxy_address)
    passed, _ = beebium.recvfrom(4096)
    assert marker_of(passed) == 1
    assert proxy.counters.dropped == 0


def test_clearing_rules_releases_anything_still_held(endpoints):
    """A held datagram must not be stranded when a test finishes with it."""
    beebium, bridge, proxy = endpoints

    send_to_proxy(beebium, proxy, aun_datagram(AunType.UNICAST, 0))
    _, proxy_address = bridge.recvfrom(4096)

    proxy.hold_next(Direction.INBOUND, types=(AunType.ACK,), after=5)
    bridge.sendto(aun_datagram(AunType.ACK, 1), proxy_address)

    with pytest.raises(socket.timeout):
        beebium.settimeout(0.3)
        beebium.recvfrom(4096)

    beebium.settimeout(RECV_TIMEOUT)
    proxy.clear_rules()

    released, _ = beebium.recvfrom(4096)
    assert marker_of(released) == 1
