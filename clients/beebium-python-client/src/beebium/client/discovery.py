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

"""mDNS/DNS-SD service discovery for Beebium servers.

This module provides discovery of Beebium servers advertised via DNS-SD.
It requires the optional 'zeroconf' dependency to be installed:

    pip install beebium[discovery]

Example usage:
    from beebium.client.discovery import browse, MachineDiscovery

    # Simple one-shot discovery
    for machine in browse(timeout=5.0):
        print(f"Found: {machine.name} at {machine.host}:{machine.port}")
        print(f"  UUID: {machine.uuid}")
        print(f"  Model: {machine.model}")

    # Continuous discovery with callbacks
    def on_found(machine):
        print(f"Discovered: {machine.name}")

    def on_removed(name):
        print(f"Removed: {name}")

    discovery = MachineDiscovery(on_found=on_found, on_removed=on_removed)
    discovery.start()
    # ... do other work ...
    discovery.stop()
"""

from __future__ import annotations

import ipaddress
from dataclasses import dataclass
from typing import TYPE_CHECKING, cast

if TYPE_CHECKING:
    from collections.abc import Callable, Iterable, Iterator

    from zeroconf import ServiceListener, Zeroconf

# Service type for Beebium discovery
SERVICE_TYPE = "_beebium._tcp.local."


@dataclass(frozen=True)
class DiscoveredMachine:
    """Information about a discovered Beebium server.

    Attributes:
        name: Instance name (from DNS-SD, e.g., "BBC Model B")
        host: Hostname or IP address
        port: gRPC port number
        uuid: Machine UUID (from TXT record)
        model: Machine model type (from TXT record, e.g., "ModelB")
        provenance: Launch provenance type (from TXT record)
        version: Server version (from TXT record, may be empty)
        econet_station: Econet station number (1-254), or None if Econet not enabled
        econet_net: Econet network number, or None if not in AUN mode
        econet_aun_port: AUN UDP port, or None if not in AUN mode
    """

    name: str
    host: str
    port: int
    uuid: str
    model: str
    provenance: str
    version: str = ""
    econet_station: int | None = None
    econet_net: int | None = None
    econet_aun_port: int | None = None


class MachineDiscovery:
    """Continuous mDNS discovery of Beebium servers.

    This class provides event-driven discovery with callbacks for
    when machines appear and disappear from the network.

    Usage:
        def on_found(machine: DiscoveredMachine):
            print(f"Found: {machine.name}")

        def on_removed(name: str):
            print(f"Lost: {name}")

        discovery = MachineDiscovery(on_found=on_found, on_removed=on_removed)
        discovery.start()
        try:
            # Do other work while discovery runs in background
            time.sleep(60)
        finally:
            discovery.stop()
    """

    def __init__(
        self,
        on_found: Callable[[DiscoveredMachine], None] | None = None,
        on_removed: Callable[[str], None] | None = None,
    ):
        """Initialize machine discovery.

        Args:
            on_found: Callback invoked when a new machine is discovered.
                Receives a DiscoveredMachine object.
            on_removed: Callback invoked when a machine is no longer visible.
                Receives the machine's instance name.
        """
        self._on_found = on_found
        self._on_removed = on_removed
        self._zeroconf = None
        self._browser = None
        self._machines: dict[str, DiscoveredMachine] = {}

    def start(self) -> None:
        """Start discovery. Runs in background thread."""
        # Lazy import to allow the module to load even without zeroconf installed
        try:
            from zeroconf import ServiceBrowser, Zeroconf
        except ImportError as e:
            raise ImportError(
                "zeroconf is required for mDNS discovery. Install it with: pip install beebium[discovery]"
            ) from e

        self._zeroconf = Zeroconf()
        self._browser = ServiceBrowser(self._zeroconf, SERVICE_TYPE, cast("ServiceListener", self))

    def stop(self) -> None:
        """Stop discovery and release resources."""
        if self._browser:
            self._browser.cancel()
            self._browser = None
        if self._zeroconf:
            self._zeroconf.close()
            self._zeroconf = None
        self._machines.clear()

    @property
    def machines(self) -> dict[str, DiscoveredMachine]:
        """Currently known machines (name -> DiscoveredMachine)."""
        return dict(self._machines)

    # ServiceListener interface methods (called by zeroconf)

    def add_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        """Called when a service is discovered."""
        self._handle_service_update(zc, type_, name)

    def update_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        """Called when a service is updated."""
        self._handle_service_update(zc, type_, name)

    def remove_service(self, zc: Zeroconf, type_: str, name: str) -> None:
        """Called when a service is removed."""
        # Extract instance name (remove service type suffix)
        instance_name = name.removesuffix("." + type_)

        if instance_name in self._machines:
            del self._machines[instance_name]
            if self._on_removed:
                self._on_removed(instance_name)

    def _handle_service_update(self, zc: Zeroconf, type_: str, name: str) -> None:
        """Process a service add or update event."""
        info = zc.get_service_info(type_, name)
        if not info:
            return

        # Extract instance name
        instance_name = name.removesuffix("." + type_)

        # Parse TXT records
        txt = {}
        if info.properties:
            for key, value in info.properties.items():
                if isinstance(key, bytes):
                    key = key.decode("utf-8", errors="replace")
                if isinstance(value, bytes):
                    value = value.decode("utf-8", errors="replace")
                txt[key] = value

        # Get host address
        addresses = info.parsed_addresses()
        host = addresses[0] if addresses else (info.server or "")

        machine = DiscoveredMachine(
            name=instance_name,
            host=host,
            port=info.port or 0,
            uuid=txt.get("uuid", ""),
            model=txt.get("model", ""),
            provenance=txt.get("provenance", ""),
            version=txt.get("version", ""),
            econet_station=int(txt["econet_station"]) if "econet_station" in txt else None,
            econet_net=int(txt["econet_net"]) if "econet_net" in txt else None,
            econet_aun_port=int(txt["econet_aun_port"]) if "econet_aun_port" in txt else None,
        )

        is_new = instance_name not in self._machines
        self._machines[instance_name] = machine

        if is_new and self._on_found:
            self._on_found(machine)


def browse(timeout: float = 5.0) -> Iterator[DiscoveredMachine]:
    """Discover Beebium servers on the local network.

    This is a simple one-shot discovery function that browses for
    the specified timeout and yields all discovered machines.

    Args:
        timeout: How long to browse in seconds (default 5.0).

    Yields:
        DiscoveredMachine for each server found.

    Example:
        for machine in browse(timeout=3.0):
            print(f"Found {machine.name} at {machine.host}:{machine.port}")
    """
    import time

    discovered: dict[str, DiscoveredMachine] = {}

    def on_found(machine: DiscoveredMachine) -> None:
        discovered[machine.name] = machine

    discovery = MachineDiscovery(on_found=on_found)
    discovery.start()

    try:
        time.sleep(timeout)
    finally:
        discovery.stop()

    yield from discovered.values()


def is_available() -> bool:
    """Check if mDNS discovery is available.

    Returns True if the zeroconf library is installed and a Zeroconf instance
    can be constructed. This is a weak check: constructing succeeds on hosts
    where multicast is nonetheless unroutable, so it says nothing about whether
    a service can actually be observed. Use multicast_loopback_available() when
    that guarantee is needed.
    """
    try:
        from zeroconf import Zeroconf

        zc = Zeroconf()
        zc.close()
        return True
    except Exception:
        return False


def _advertisable_ipv4_addresses(candidate_ips: Iterable[object]) -> list[str]:
    """The IPv4 addresses a real mDNS advertiser would announce on.

    Routable interfaces only: loopback and link-local are excluded, and the
    IPv6 addresses ifaddr yields (as tuples, not strings) are skipped. The order
    of the input is preserved and duplicates are removed.
    """
    result: list[str] = []
    for candidate in candidate_ips:
        if not isinstance(candidate, str):
            continue
        try:
            parsed = ipaddress.ip_address(candidate)
        except ValueError:
            continue
        if parsed.version == 4 and not parsed.is_loopback and not parsed.is_link_local:
            if candidate not in result:
                result.append(candidate)
    return result


def _non_loopback_ipv4_addresses() -> list[str]:
    """The host's routable IPv4 interface addresses, via ifaddr (a zeroconf
    dependency, so present whenever zeroconf is)."""
    import ifaddr

    candidate_ips = [ip.ip for adapter in ifaddr.get_adapters() for ip in adapter.ips]
    return _advertisable_ipv4_addresses(candidate_ips)


def multicast_loopback_available(timeout: float = 5.0, *, _register: bool = True) -> bool:
    """Whether mDNS discovery can actually observe a service on this host.

    Registers a throwaway DNS-SD service through one zeroconf instance and
    browses for it through a second, independent one. Seeing it proves that a
    multicast announcement sent from a real interface loops back and is received
    on UDP 5353. Some sandboxed CI runners have no multicast route on their LAN
    interface: zeroconf logs "No route to host" on 5353 and a browser observes
    nothing, whatever is advertising. Discovery cannot work there, and this
    returns False.

    Two deliberate choices make this a faithful predictor of the real browse:

    - The registrar advertises only on the routable interfaces (never on the
      loopback interface), because the server's advertiser -- mDNSResponder or
      avahi-daemon -- announces on real interfaces, not lo0. Advertising over
      loopback would prove a path the real service never uses: a runner whose
      loopback multicast works but whose LAN interface has no route would then
      look usable when it is not.
    - The browser uses zeroconf's default interface set, exactly as
      MachineDiscovery does, so what it can observe here is what the real
      discovery can observe.

    Returns False if the zeroconf extra is not installed, or if the host has no
    routable IPv4 interface to advertise on.
    """
    try:
        from zeroconf import ServiceBrowser, ServiceInfo, Zeroconf
    except ImportError:
        return False

    import socket
    import threading
    import uuid

    addresses = _non_loopback_ipv4_addresses()
    if not addresses:
        # Nothing a real advertiser could announce on, so the real path cannot
        # be exercised; treat discovery as unusable here.
        return False

    token = uuid.uuid4().hex
    probe_type = "_beebium-probe._tcp.local."
    full_name = f"{token}.{probe_type}"
    info = ServiceInfo(
        probe_type,
        full_name,
        addresses=[socket.inet_aton(addresses[0])],
        port=1,
        properties={b"token": token.encode("ascii")},
        server=f"{token}.local.",
    )

    seen = threading.Event()

    class _ProbeListener:
        def add_service(self, zc: Zeroconf, type_: str, name: str) -> None:
            if name == full_name:
                seen.set()

        def update_service(self, zc: Zeroconf, type_: str, name: str) -> None:
            if name == full_name:
                seen.set()

        def remove_service(self, zc: Zeroconf, type_: str, name: str) -> None:
            pass

    registrar: Zeroconf | None = None
    browser_zeroconf: Zeroconf | None = None
    try:
        # Browse first, so the browser is listening before the announcement.
        browser_zeroconf = Zeroconf()
        ServiceBrowser(browser_zeroconf, probe_type, cast("ServiceListener", _ProbeListener()))
        if _register:
            registrar = Zeroconf(interfaces=addresses)
            registrar.register_service(info)
        return seen.wait(timeout)
    except OSError:
        # A host with no multicast route raises here rather than merely timing
        # out; either way discovery is not usable.
        return False
    finally:
        if registrar is not None:
            try:
                registrar.unregister_service(info)
            except Exception:
                pass
            registrar.close()
        if browser_zeroconf is not None:
            browser_zeroconf.close()
