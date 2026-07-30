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

"""A UDP relay that perturbs AUN traffic under test control.

A real bridge is a fidelity anchor but a poor reproducer: it reorders only
under load, non-deterministically, and cannot be asked to drop a datagram on
request. A test that waits for a race to happen is a flaky test.

This sits between Beebium and a real bridge and makes the race deterministic.
The traffic is genuine -- real scout timing, real fileserver replies, real ack
cadence -- and only its arrival order is ours to choose. That is the
difference between "this defect appears intermittently on someone's machine"
and "this test fails every run until the defect is fixed".

Measured motivation: 25 login/``*CAT``/``*BYE`` iterations against a real
containerised bridge produce a holding-queue count of exactly zero. On a
loopback path nothing reorders on its own, so without deliberate perturbation
the scenario that motivated the robustness programme never occurs.

See docs/discussion/pieconetbridge-aun-interop-testing.md.
"""

from __future__ import annotations

import enum
import socket
import threading
import time
from dataclasses import dataclass, field

# AUN's 8-byte header: type, port, control byte, pad, then a 4-byte handle.
AUN_HEADER_SIZE = 8


class AunType(enum.IntEnum):
    """AUN packet types, as they appear in the first header byte."""

    BROADCAST = 1
    UNICAST = 2
    ACK = 3
    NACK = 4
    IMMEDIATE = 5
    IMM_REPLY = 6


class Direction(enum.Enum):
    """Which way a datagram is travelling."""

    #: Beebium towards the bridge.
    OUTBOUND = "outbound"
    #: Bridge towards Beebium.
    INBOUND = "inbound"


def aun_type(datagram: bytes) -> int | None:
    """The AUN packet type of a datagram, or None if it is too short."""
    if len(datagram) < AUN_HEADER_SIZE:
        return None
    return datagram[0]


@dataclass
class Counters:
    """What the proxy did, so a test can assert it actually perturbed anything.

    Without these a test that silently failed to trigger its perturbation
    would pass exactly like one that triggered it and survived.
    """

    forwarded: int = 0
    held: int = 0
    released: int = 0
    dropped: int = 0
    duplicated: int = 0

    #: Holds released by their deadline rather than by traffic overtaking
    #: them. A high count means the perturbation is mostly adding latency
    #: rather than reordering anything.
    expired_holds: int = 0


@dataclass
class _HoldRule:
    """Hold the next datagram matching `types`, release it after `after` more.

    ``after=1`` swaps a datagram with the one behind it, which is the exact
    shape of a fileserver reply overtaking its own acknowledgement.

    A held datagram is also released once `max_hold_seconds` has elapsed, even
    if nothing followed it. Without that the instrument can capture the last
    datagram of a conversation for ever and deadlock it -- which is not
    reordering but loss, and would be blamed on whatever is under test.
    Reordering on a real network is always a bounded delay.
    """

    direction: Direction
    types: frozenset[int]
    after: int
    remaining_uses: int = 1
    max_hold_seconds: float = 0.5

    held: bytes | None = field(default=None, repr=False)
    countdown: int = 0
    held_at: float = 0.0


@dataclass
class _DropRule:
    direction: Direction
    types: frozenset[int]
    remaining_uses: int = 1


class PerturbationProxy:
    """Relay AUN traffic between Beebium and a bridge, perturbing on request.

    Beebium is pointed at this proxy instead of at the bridge, and the bridge
    sees traffic arriving from the proxy's socket. Beebium's address is learned
    from its first datagram rather than configured, since its ephemeral port is
    not known until it binds.
    """

    def __init__(self, bridge_address: str, bridge_port: int,
                 listen_host: str = "127.0.0.1"):
        self._bridge = (bridge_address, bridge_port)
        self._socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._socket.bind((listen_host, 0))
        self._socket.settimeout(0.2)
        self._beebium: tuple[str, int] | None = None

        self._lock = threading.Lock()
        self._hold_rules: list[_HoldRule] = []
        self._drop_rules: list[_DropRule] = []
        self._counters = Counters()

        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True,
                                        name="aun-perturbation-proxy")
        self._thread.start()

    # ---- Endpoint ----

    @property
    def port(self) -> int:
        """The port Beebium should send to."""
        return self._socket.getsockname()[1]

    @property
    def address(self) -> str:
        return self._socket.getsockname()[0]

    # ---- Policy ----

    def hold_next(self, direction: Direction, *,
                  types: tuple[int, ...] = (),
                  after: int = 1, uses: int = 1,
                  max_hold_seconds: float = 0.5) -> None:
        """Hold the next matching datagram until `after` more have passed.

        With ``types=(AunType.ACK,)`` and ``after=1`` this makes a fileserver's
        reply overtake its own acknowledgement, deterministically -- the
        ordering that destroyed the reply before the holding queue existed.
        """
        with self._lock:
            self._hold_rules.append(_HoldRule(
                direction=direction,
                types=frozenset(types),
                after=after,
                remaining_uses=uses,
                max_hold_seconds=max_hold_seconds,
            ))

    def drop_next(self, direction: Direction, *,
                  types: tuple[int, ...] = (), uses: int = 1) -> None:
        """Discard the next matching datagram outright."""
        with self._lock:
            self._drop_rules.append(_DropRule(
                direction=direction,
                types=frozenset(types),
                remaining_uses=uses,
            ))

    def clear_rules(self) -> None:
        """Return to plain relaying, releasing anything still held."""
        with self._lock:
            for rule in self._hold_rules:
                if rule.held is not None:
                    self._deliver(rule.direction, rule.held)
                    self._counters.released += 1
            self._hold_rules.clear()
            self._drop_rules.clear()

    @property
    def counters(self) -> Counters:
        with self._lock:
            return Counters(**vars(self._counters))

    # ---- Lifecycle ----

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(timeout=5)
        self._socket.close()

    def __enter__(self) -> PerturbationProxy:
        return self

    def __exit__(self, *exc_info) -> None:
        self.stop()

    # ---- Relay ----

    def _run(self) -> None:
        while not self._stop.is_set():
            try:
                datagram, sender = self._socket.recvfrom(65535)
            except socket.timeout:
                # Quiet moments are when a held datagram's deadline is
                # noticed, so the timeout is load-bearing rather than idle.
                with self._lock:
                    self._release_expired()
                continue
            except OSError:
                return

            if sender[0] == self._bridge[0] and sender[1] == self._bridge[1]:
                direction = Direction.INBOUND
            else:
                # Anything that is not the bridge is Beebium. Learning its
                # address rather than requiring it up front matters because
                # its port is ephemeral and not known until it binds.
                self._beebium = sender
                direction = Direction.OUTBOUND

            with self._lock:
                self._release_expired()
                self._handle(direction, datagram)

    def _handle(self, direction: Direction, datagram: bytes) -> None:
        packet_type = aun_type(datagram)

        # A held datagram counts down against everything that follows it,
        # released once enough has gone past to have overtaken it.
        for rule in self._hold_rules:
            if rule.held is None or rule.direction is not direction:
                continue
            rule.countdown -= 1
            if rule.countdown <= 0:
                pending, rule.held = rule.held, None
                self._forward_now(direction, datagram)
                self._deliver(direction, pending)
                self._counters.released += 1
                return

        for rule in self._drop_rules:
            if (rule.remaining_uses > 0
                    and rule.direction is direction
                    and self._matches(rule.types, packet_type)):
                rule.remaining_uses -= 1
                self._counters.dropped += 1
                return

        for rule in self._hold_rules:
            if (rule.held is None
                    and rule.remaining_uses > 0
                    and rule.direction is direction
                    and self._matches(rule.types, packet_type)):
                rule.remaining_uses -= 1
                rule.held = datagram
                rule.countdown = rule.after
                rule.held_at = time.monotonic()
                self._counters.held += 1
                return

        self._forward_now(direction, datagram)

    def _release_expired(self) -> None:
        """Release held datagrams whose deadline has passed."""
        now = time.monotonic()
        for rule in self._hold_rules:
            if rule.held is None:
                continue
            if now - rule.held_at >= rule.max_hold_seconds:
                pending, rule.held = rule.held, None
                self._deliver(rule.direction, pending)
                self._counters.released += 1
                self._counters.expired_holds += 1

    @staticmethod
    def _matches(types: frozenset[int], packet_type: int | None) -> bool:
        if not types:
            return True
        return packet_type is not None and packet_type in types

    def _forward_now(self, direction: Direction, datagram: bytes) -> None:
        self._deliver(direction, datagram)
        self._counters.forwarded += 1

    def _deliver(self, direction: Direction, datagram: bytes) -> None:
        if direction is Direction.OUTBOUND:
            self._socket.sendto(datagram, self._bridge)
        elif self._beebium is not None:
            self._socket.sendto(datagram, self._beebium)
