# Econet/AUN Stack Integration

This document records the work programme that exposed the Econet/AUN networking core across the Beebium stack: presets, gRPC, service discovery, the Python client, and the macOS client. All five phases are complete. For how the networking works today -- transports, the AUN peer set and map file, discovery, station selection -- see [`networking.md`](networking.md); for writing a new transport, see [`howto_write_an_econet_transport.md`](howto_write_an_econet_transport.md).

The Econet/AUN networking core (MC68B54 ADLC emulation, EconetSocket, FourWayHandshake) runs over a `NetworkBackend` supplied by a transport extension: `AunBackend` (UDP/IP, from the built-in `aun` transport) or `PiconetBackend` (a USB-attached Piconet device on a real Econet wire, from the `piconet` plugin), with `TestBackend` as an in-process test double and as the disconnected stub fitted when no transport is configured. It has been validated end-to-end against a real BBC Microcomputer talking via real Econet to a Beebium-emulated Level 3 File Server, and against a real Acorn Level 3 Fileserver running in BeebEm via AUN.

## Phases

| Phase | Summary | Status |
|-------|---------|--------|
| 0 | Update `docs/networking.md` to match implementation | **Done** |
| 1 | Preset integration (JSON format, schema, apply) | **Done** (AUN + Piconet) |
| 2 | gRPC proto + C++ service | **Done** -- transport-agnostic `EconetService`; transport-specific RPCs on `AunService` / `PiconetService` over `ExtensionRpc` |
| 3 | Service discovery metadata | **Done** |
| 4 | Python client | **Done** |
| 5 | macOS client | **Done** (Network sidebar); no Econet section in the configuration editor |

## Design Decisions

These decisions were made during planning and still describe the implementation:

- **Read/write gRPC API**: The EconetService supports both status queries and runtime configuration (enable/disable Econet, set the station number), following the DiscService pattern. Peer management is transport-specific and lives on the transport's own service (`AunService`).

- **Frame-level event streaming**: `EconetService.SubscribeEconetEvents` streams frame send/receive events, handshake stage changes, and connection state changes, following the `SubscribeDiscEvents` pattern. `WatchEconetStatus` pushes a fresh status snapshot on every change.

- **DiscService as the primary pattern**: The Econet integration (proto design, C++ service template, Python wrapper, Swift client, sidebar UI) follows the corresponding Disc subsystem implementation.

- **IP addresses as strings in proto**: Peer addresses use dotted-quad strings (`"192.168.1.100"`) rather than packed uint32, for client ergonomics.

- **Observable backend decorator for frame events**: Frame observation uses a decorator in the backend chain rather than modifying the `NetworkBackend` interface. `EconetSocket::enable` builds the chain `Mc6854 -> FourWayHandshake -> [SpeedGate] -> ObservableBackend -> <transport backend>`; the `SpeedGate` is present only for a transport that requires real-time pacing (Piconet), and observation sits directly above the wire, so it records what actually crossed the transport.

- **Peer resolution is an abstraction, not just static config**: Peers can come from the command line or a preset, a runtime gRPC call, the per-user map file, mDNS discovery, or a subnet rule.

  - **The peer set is the single source of truth (in the transport, not the backend)**: The desired peer world lives in `AunPeerSet`, owned by `AunEconetTransportExtension` (#54/#55). All peer sources converge on it; it resolves one winner per `(net, stn)` and applies that routing view to the live `AunBackend` via `replace_peers`. The backend holds only the resolved view for its socket, so the peer set outlives it: entries added before the socket is up wait in the set, and runtime (`Api`) entries survive backend recreation.

  - **Peer edits go through the transport**: `AunService.AddPeer` / `RemovePeer` call the extension's `add_api_peer` / `remove_api_peer`, so they work whether or not a backend exists yet. `EconetService.EnableEconet` brings the network up through the configured transport's `create_backend`, so the extension owns the backend it reports on. The gRPC service reaches the live backend via `EconetSocket::backend()` (or a co-owning `backend_shared()` from a thread other than the emulation thread).

  - **Peer provenance**: `AunPeerProvenance` has five values -- `Api`, `Launch`, `MapFile`, `Discovered`, `Subnet` -- resolved by fixed precedence, highest first in that order. Each source keeps its own entry, so removing a winner falls back to the next source present rather than dropping the station. The mDNS subscriber writes `Discovered` entries into the same set; `Subnet` entries are derived from a subnet rule (`--aun subnet=` or the map file's `subnets`).

  - **Discovery mechanisms**: Beebium-to-Beebium discovery over mDNS (`_aun._udp`) is implemented, with a `discovery=on|announce|browse|off` switch (#158), and `--station auto` chooses a free station number by browsing and claiming over the same records (#67) rather than through a central allocator. Not implemented: AUN broadcast discovery for interop with other AUN implementations, a user-specified rendezvous server, and a DHCP-like station allocator (see `docs/discussion/dynamic-station-config-protocol.md`).

---

## Phase 0: Documentation Update — Done

Networking documentation in `docs/networking.md` was reviewed and updated as part of the piconet branch. Three transport backends (`AunBackend`, `PiconetBackend`, `TestBackend`) are now documented with selection guidance, mutual-exclusion rules, and pointers to design and limitation docs.

---

## Phase 1: Preset Integration — Done

Both transports are configured via a single `econet.transport` object that names the transport extension and carries its parameters as a flat key/value map. The `econet-transport-extensions` branch unified what was previously separate per-backend keys.

### Preset JSON format — AUN

```json
{
  "econet": {
    "station": 5,
    "transport": {
      "name": "aun",
      "parameters": { "port": "32768", "map": "0.254@127.0.0.1@32769" }
    }
  }
}
```

### Preset JSON format — Piconet

```json
{
  "econet": {
    "station": 32,
    "transport": {
      "name": "piconet",
      "parameters": { "device_path": "/dev/tty.usbmodem101" }
    }
  }
}
```

The `name` field selects the transport extension (`aun`, `piconet`, or any future extension). `parameters` is the same key/value map the CLI populates from `--<extension> key=value:key=value`; a repeatable (list) parameter such as AUN's `map` or `subnet` takes either a single string or a JSON array of strings. Only one `transport` is permitted per `econet` block on BBC machine variants; per-machine cardinality is enforced at machine-setup time, not in the preset loader. `station` is an integer 1-254, or the string `"auto"` / `"auto:<lo>-<hi>"` to choose a free number at launch (AUN only; the bundled `model-b-disc-aun-auto` preset uses it).

A `--aun` (or `--piconet`) on the command line overrides the preset's transport: with the same transport name the parameters merge, the command line winning key by key; with a different name the command line's transport replaces the preset's (#150). Only the keys you type on the command line override the preset -- keys you omit keep the preset's value, even though the transport fills them with manifest defaults internally (#166); see [cli.md](cli.md).

The legacy preset keys `econet.aun_port` and `econet.piconet` are rejected with a message pointing at the new shape. Other unknown keys in the `econet` section, including the old `econet.aun_map`, are ignored.

### Key files

- `src/server/include/beebium/server/PresetLoader.hpp` — `PresetEconetConfig`, `PresetTransportConfig`, `parse_econet_section()`
- `src/server/include/beebium/server/ServerMain.hpp` — the preset's transport becomes an extension instance (merged with any CLI `--aun` / `--piconet` by `merge_preset_econet_transport`) and is routed through the same dispatch as the CLI
- `tests/test_preset_loader.cpp` — Econet preset parsing tests including legacy-shape rejection
- `tests/test_cli.cpp` — CLI parsing tests for `--aun port=...` and `--piconet device_path=...`

---

## Phase 2: gRPC Service — Done

`EconetService` (`src/service/proto/econet.proto`) is transport-agnostic: `GetEconetStatus`, `EnableEconet`, `DisableEconet`, `SetStationId`, `SubscribeEconetEvents` and `WatchEconetStatus`. `EconetTransportService` (`econet_transport.proto`) reports which transport extensions are loaded and which is active (`ListTransports`, `GetActiveTransport`), each with its instance id.

Transport-specific RPCs are not EconetService methods. AUN's are defined by `AunService` (`src/extensions/aun/aun.proto`): `SetConnected`, `AddPeer`, `RemovePeer`, `ListPeers`, `GetStatus`, and the map-file methods `ReloadMap`, `AddMapPeer`, `RemoveMapPeer`, `AddMapSubnet`, `RemoveMapSubnet` and `ListMap`. Piconet's `PiconetService` has a single `GetStatus`. Both are served over the core's generic `ExtensionRpc` channel by the extension's hand-written dispatcher (`AunDispatcher`, `PiconetDispatcher`), not as plugin-hosted gRPC services; the transport libraries therefore link protobuf but not gRPC. An `ExtensionRpc.Invoke` carries the transport instance id (the one `EconetTransportService` reports) in `extension_id` to select which transport it reaches; an empty id routes by service name while exactly one transport offers it, and is an ambiguity error once more than one does (#56; see [`networking.md`](networking.md#routing-a-transports-typed-rpcs-extensionrpc)).

Key messages:
- `GetEconetStatusResponse` includes nested `AdlcStatus` (CR1-4, SR1-2, FIFO state) and `HandshakeStatus` (stage, flag fill)
- `EconetEvent` carries event type, timestamp, and optional frame info / peer info / handshake stage
- `EconetFrameInfo` carries frame type, addresses, port, control byte, data length, and (possibly truncated) payload

### Key files

- `src/service/proto/econet.proto`, `src/service/proto/econet_transport.proto` — proto definitions
- `src/service/include/beebium/service/EconetService.hpp` — template service implementation
- `src/service/include/beebium/service/Server.hpp` — registration
- `src/core/include/beebium/econet/EconetSocket.hpp` — `backend()`, `backend_shared()`, `aun_mode()`
- `src/core/include/beebium/econet/AunBackend.hpp` — `list_peers()`, `replace_peers()`
- `src/extensions/aun/aun.proto`, `src/extensions/aun/AunDispatcher.hpp` — AUN-specific RPCs
- `tests/test_grpc_econet.cpp`, `tests/test_grpc_econet_transport_service.cpp`, `tests/test_grpc_aun_service.cpp` — service tests

---

## Phase 3: Service Discovery Metadata — Done

The server's gRPC mDNS advertisement (`--advertise`) carries Econet TXT records so discovery clients can see which machines have Econet fitted and their station numbers.

### TXT records

When Econet is enabled: `econet_station=N`; when the backend is AUN, also `econet_net=N` and `econet_aun_port=N`. The records are set when the server starts advertising.

(These are on the `_beebium._tcp` gRPC service record. The AUN transport's own `_aun._udp` peer announcements, with their `since=` and `impl-identity` records, are described in [`networking.md`](networking.md).)

### Key files

- `src/service/include/beebium/service/Server.hpp` — adds the TXT records in `Server::start`
- `clients/beebium-python-client/src/beebium/client/discovery.py` — parses `econet_station`
- `clients/macos/Beebium/Beebium/DiscoveryClient.swift` — parses `econet_station`

---

## Phase 4: Python Client — Done

The Python client splits along the same line as the gRPC services: an
`Econet` wrapper around the transport-agnostic `EconetService`
(`bbc.econet`), an `EconetTransport` wrapper around `EconetTransportService`
(`bbc.transport`), and per-transport adapters for the transport-specific
services. The AUN adapter `Aun` (package `beebium.ext.econet.aun`) is reached
with `bbc.transport[Aun]` or `Aun.attach(bbc)`, and routes its calls by the
transport's instance id; the Piconet adapter is `beebium.ext.econet.piconet`.

### Wrapper class outline

```python
class Econet:                              # wraps EconetService
    status -> EconetStatus                 # generic: enabled, station_id, ADLC
    watch_status(min_interval_ms=0) -> Iterator[EconetStatus]
    is_enabled -> bool
    station_id -> int
    enable(...)                            # transport-agnostic
    set_station_id(station_id)
    disable()
    events(...) -> Iterator[EconetEvent]


class Aun:                                  # AunService over ExtensionRpc
    status -> AunStatus                     # port, peer count, map file, discovery mode
    peers -> list[PeerInfo]                 # resolved routing view, with source
    set_connected(connected: bool)
    add_peer(net, stn, ip_address, port)
    remove_peer(net, stn)
    reload_map()
    add_map_peer(...) / remove_map_peer(net, stn)
    add_map_subnet(net, subnet, label="") / remove_map_subnet(net)
    list_map() -> MapListing
```

### Key files

- `clients/beebium-python-client/src/beebium/client/econet.py` — `Econet`
- `clients/beebium-python-client/src/beebium/client/econet_transport.py` — `EconetTransport`
- `clients/beebium-python-client/src/beebium/ext/econet/aun/__init__.py` — `Aun`
- `clients/beebium-python-client/src/beebium/client/client.py` — `econet` and `transport` properties
- `clients/beebium-python-client/tests/test_econet.py`, `test_econet_transport.py`, `test_aun.py` — tests

---

## Phase 5: macOS Client — Done

### UI integration

The macOS sidebar's `.network` mode (in `SidebarMode.swift`, with the "network" SF Symbol icon) hosts the Econet controls: a transport-agnostic status section from `EconetClient`, and the active transport's own panel, rendered from the extension's server-driven UI (`ExtensionViewRenderer`). The AUN panel includes the editable "Shared AUN map". See [`networking.md`](networking.md) ("Network sidebar (macOS GUI)").

The configuration editor (preset UI) has no Econet section; Econet is configured through presets and the command line.

### Key files

- `clients/macos/Beebium/Beebium/EconetClient.swift` — gRPC client wrapper (`@MainActor`, `ObservableObject`, `Disconnectable`)
- `clients/macos/Beebium/Beebium/EconetTransportsClient.swift` — `EconetTransportService` client
- `clients/macos/Beebium/Beebium/SidebarModeContent.swift` — routes `.network` to `NetworkModeView`
- `clients/macos/Beebium/Beebium/ContentView.swift` — creates and registers `EconetClient`
- `clients/macos/Beebium/Beebium/ExtensionViewRenderer.swift` — renders the transport's panel
- Generated Swift proto stubs from `econet.proto` and `econet_transport.proto`
