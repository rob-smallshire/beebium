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

"""A withdrawn machine stays withdrawn (#65).

MachineDiscovery resolves a service outside any lock, so a resolution can land
after the service has gone. These tests drive the listener methods directly
with a fake zeroconf whose resolution can be held in flight, so the ordering
that is a race on a real network is deterministic here.
"""

from __future__ import annotations

import threading
from typing import Any

from beebium.client.discovery import SERVICE_TYPE, MachineDiscovery

NAME = "Peterhouse"
FULL_NAME = f"{NAME}.{SERVICE_TYPE}"


class _FakeInfo:
    """The subset of zeroconf's ServiceInfo that MachineDiscovery reads."""

    def __init__(self) -> None:
        self.properties = {b"uuid": b"1234", b"model": b"model-b", b"provenance": b"test"}
        self.port = 48875
        self.server = "host.local."

    def parsed_addresses(self) -> list[str]:
        return ["192.0.2.1"]


class _FakeZeroconf:
    """Resolves every service from a fake cache.

    While `hold` is set, get_service_info blocks until `release` is set, so a
    resolution can be left in flight while other events are delivered.
    """

    def __init__(self) -> None:
        self.hold = threading.Event()
        self.release = threading.Event()
        self.resolving = threading.Event()

    def get_service_info(self, type_: str, name: str) -> Any:
        if self.hold.is_set():
            self.resolving.set()
            assert self.release.wait(5.0), "test never released the resolution"
        return _FakeInfo()


class _Recorder:
    def __init__(self) -> None:
        self.events: list[tuple[str, str]] = []

    def found(self, machine: Any) -> None:
        self.events.append(("found", machine.name))

    def removed(self, name: str) -> None:
        self.events.append(("removed", name))


def _discovery() -> tuple[MachineDiscovery, _Recorder]:
    recorder = _Recorder()
    return MachineDiscovery(on_found=recorder.found, on_removed=recorder.removed), recorder


def test_an_add_then_remove_is_reported_in_order() -> None:
    discovery, recorder = _discovery()
    zc = _FakeZeroconf()

    discovery.add_service(zc, SERVICE_TYPE, FULL_NAME)  # type: ignore[arg-type]
    assert NAME in discovery.machines
    discovery.remove_service(zc, SERVICE_TYPE, FULL_NAME)  # type: ignore[arg-type]

    assert discovery.machines == {}
    assert recorder.events == [("found", NAME), ("removed", NAME)]


def test_a_resolution_landing_after_removal_does_not_re_add() -> None:
    discovery, recorder = _discovery()
    zc = _FakeZeroconf()
    discovery.add_service(zc, SERVICE_TYPE, FULL_NAME)  # type: ignore[arg-type]

    # An update's resolution is in flight when the goodbye arrives.
    zc.hold.set()
    late = threading.Thread(
        target=discovery.update_service,
        args=(zc, SERVICE_TYPE, FULL_NAME),
    )
    late.start()
    assert zc.resolving.wait(5.0)

    discovery.remove_service(zc, SERVICE_TYPE, FULL_NAME)  # type: ignore[arg-type]

    zc.release.set()
    late.join(5.0)
    assert not late.is_alive()

    assert discovery.machines == {}
    assert recorder.events == [("found", NAME), ("removed", NAME)]


def test_an_add_resolving_across_a_removal_does_not_re_add() -> None:
    discovery, recorder = _discovery()
    zc = _FakeZeroconf()

    # The first sighting is still resolving when the service goes away.
    zc.hold.set()
    late = threading.Thread(
        target=discovery.add_service,
        args=(zc, SERVICE_TYPE, FULL_NAME),
    )
    late.start()
    assert zc.resolving.wait(5.0)

    discovery.remove_service(zc, SERVICE_TYPE, FULL_NAME)  # type: ignore[arg-type]

    zc.release.set()
    late.join(5.0)
    assert not late.is_alive()

    assert discovery.machines == {}
    assert recorder.events == []


def test_an_update_handled_after_removal_is_ignored() -> None:
    discovery, recorder = _discovery()
    zc = _FakeZeroconf()
    discovery.add_service(zc, SERVICE_TYPE, FULL_NAME)  # type: ignore[arg-type]
    discovery.remove_service(zc, SERVICE_TYPE, FULL_NAME)  # type: ignore[arg-type]

    # An update queued before the goodbye but delivered after it, resolved
    # from records still in the cache.
    discovery.update_service(zc, SERVICE_TYPE, FULL_NAME)  # type: ignore[arg-type]

    assert discovery.machines == {}
    assert recorder.events == [("found", NAME), ("removed", NAME)]


def test_a_genuine_add_after_removal_brings_the_machine_back() -> None:
    discovery, recorder = _discovery()
    zc = _FakeZeroconf()
    discovery.add_service(zc, SERVICE_TYPE, FULL_NAME)  # type: ignore[arg-type]
    discovery.remove_service(zc, SERVICE_TYPE, FULL_NAME)  # type: ignore[arg-type]

    discovery.add_service(zc, SERVICE_TYPE, FULL_NAME)  # type: ignore[arg-type]
    assert NAME in discovery.machines

    # And updates to it are handled again.
    discovery.update_service(zc, SERVICE_TYPE, FULL_NAME)  # type: ignore[arg-type]
    assert NAME in discovery.machines
    assert recorder.events == [("found", NAME), ("removed", NAME), ("found", NAME)]
