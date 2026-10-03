# Econet/AUN Stack Integration

This document captures the work programme for integrating Econet/AUN features across the Beebium stack: presets, gRPC, service discovery, Python client, and macOS client.

The Econet/AUN networking core (MC68B54 ADLC emulation, EconetSocket, FourWayHandshake) supports three transport backends: `AunBackend` (UDP/IP), `PiconetBackend` (USB-attached Piconet device on a real Econet wire), and `TestBackend` (in-process test double). It has been validated end-to-end against a real BBC Microcomputer talking via real Econet to a Beebium-emulated Level 3 File Server, and against a real Acorn Level 3 Fileserver running in BeebEm via AUN. This document plans the remaining work to expose these capabilities through the standard Beebium interfaces.

## Phases

| Phase | Summary | Status | Depends on |
|-------|---------|--------|------------|
| 0 | Update `docs/networking.md` to match implementation | **Done** | — |
| 1 | Preset integration (JSON format, schema, apply) | **Done** (AUN + Piconet) | — |
| 2 | gRPC proto + C++ service | Partial — status surfaces both AUN and Piconet; full read/write API still pending | — |
| 3 | Service discovery metadata | Pending | Phase 2 |
| 4 | Python client | Pending | Phase 2 |
| 5 | macOS client | Pending | Phase 2 |

Phases 1 and 2 are independent and can be done in either order. Phases 3-5 all depend on the proto definition from Phase 2. Phase 0 is a documentation prerequisite that should be done first.

## Design Decisions

These decisions were made during planning and inform all phases:

- **Full read/write gRPC API**: The EconetService supports both status queries and runtime configuration (enable/disable Econet, add/remove peers), following the DiscService pattern which supports `InstallDiscController`, `InsertDisc`, etc.

- **Frame-level event streaming**: The gRPC service includes a `SubscribeEconetEvents` streaming RPC for frame send/receive events, handshake stage changes, and connection state changes. Follows the `SubscribeDiscEvents` pattern.

- **DiscService as the primary pattern**: All aspects of the Econet integration (proto design, C++ service template, Python wrapper, Swift client, sidebar UI) follow the corresponding Disc subsystem implementation.

- **IP addresses as strings in proto**: Peer addresses use dotted-quad strings (`"192.168.1.100"`) rather than packed uint32, for client ergonomics.

- **Observable backend decorator for frame events**: Frame observation uses a decorator in the backend chain (`FourWayHandshake -> ObservableBackend -> AunBackend`) rather than modifying the `NetworkBackend` interface. This follows the existing decorator pattern.

- **Peer resolution must be an abstraction, not just static config**: The current `--aun map=...` mechanism requires users to know IP addresses and ports up front, which is at odds with modern networking (DHCP, dynamic IPs, mDNS). The `AunBackend` already has the right runtime mutation API (`add_peer`/`remove_peer`), but the architecture must ensure this is accessible to multiple peer sources — not just CLI args and gRPC calls. Considerations:

  - **The peer set is the single source of truth (in the transport, not the backend)**: As of #55 the desired peer world lives in `AunPeerSet`, owned by `AunEconetTransportExtension`. All peer sources (CLI, presets, gRPC, discovery) converge on it, and it resolves one winner per `(net, stn)` and applies that routing view to the live `AunBackend` via `replace_peers`. The backend no longer ranks sources; it holds only the resolved view for its socket.

  - **`EconetSocket` must expose the backend chain**: The gRPC service and any in-process peer source reach the live backend via `EconetSocket::backend()`; the transport's `AunPeerSet` applies to it. Peer *edits* go through the transport (`add_api_peer`/`remove_api_peer`), so they work whether or not a backend exists yet.

  - **Peer provenance**: Implemented as a four-value `AunPeerProvenance` (`Api`, `Launch`, `MapFile`, `Discovered`) on each peer-set entry, resolved by fixed precedence (highest first: `Api`, `Launch`, `MapFile`, `Discovered`). Each source keeps its own entry, so removing a winner falls back to the next source present rather than dropping the station. Discovered peers are managed by the mDNS subscriber writing `Discovered` entries into the same set.

  - **Candidate discovery mechanisms** (future work, not part of the current programme):
    - **Beebium mDNS**: Beebium already advertises itself via mDNS for gRPC service discovery. Econet station metadata (Phase 3) could be used by other Beebium instances to auto-discover peers. This is the most natural fit for Beebium-to-Beebium networking.
    - **AUN broadcast discovery**: The original AUN protocol included broadcast announcements. This would enable interop with other AUN implementations (BeebEm, RISC OS).
    - **User-specified discovery server**: A central rendezvous point for peers that aren't on the same subnet.
    - **DSCP (Dynamic Station Configuration Protocol)**: A DHCP analogue for Econet — a DSCP server assigns station numbers from a pool, so instances can launch with `--station auto` rather than manually coordinating IDs. The client would query the server early in the `enable()` path and proceed with the assigned station ID. Just an idea for now, but potentially worth exploring as the number of Beebium instances on a network grows.

  - **No changes needed now**: The current `add_peer`/`remove_peer` API on `AunBackend` is the right seam. Phase 2's `EconetSocket::backend()` accessor completes the access path. Future discovery work is additive — it calls into the existing API without requiring changes to `AunBackend`, `FourWayHandshake`, or `Mc6854`.

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

The `name` field selects the transport extension (`aun`, `piconet`, or any future extension). `parameters` is the same key/value map the CLI populates from `--<extension> key=value:key=value`. Only one `transport` is permitted per `econet` block on BBC machine variants; per-machine cardinality is enforced at machine-setup time, not in the preset loader.

The legacy preset keys `econet.aun_port`, `econet.aun_map`, and `econet.piconet.device_path` were removed; presets that still use them fail to load with a message pointing at the new shape.

### Key files

- `src/server/include/beebium/server/PresetLoader.hpp` — `PresetEconetConfig`, `PresetTransportConfig`, `parse_econet_section()`
- `src/server/include/beebium/server/ServerMain.hpp` — `apply_preset()` converts a preset transport into an `ExtensionInstance`; the early-pass transport-registry filter then routes it through the same dispatch as the CLI
- `tests/test_preset_loader.cpp` — Econet preset parsing tests including legacy-shape rejection
- `tests/test_cli.cpp` — CLI parsing tests for `--aun port=...` and `--piconet device_path=...`

---

## Phase 2: gRPC Service

**Status:** `EconetService` is implemented, with transport-agnostic operations (`GetEconetStatus`, `EnableEconet`, `DisableEconet`, `SetStationId`, `SubscribeEconetEvents`). AUN-specific RPCs — `SetConnected`, `AddPeer`, `RemovePeer`, `ListPeers`, plus a richer `GetStatus` — are defined by `AunService` (`src/extensions/aun/aun.proto`) and served over the core's generic `ExtensionRpc` channel by the AUN extension's hand-written `AunDispatcher`, not as a plugin-hosted gRPC service; the AUN library therefore links protobuf but not gRPC. An `ExtensionRpc.Invoke` carries the transport instance id (the one `EconetTransportService` reports) in `extension_id` to select which transport it reaches; an empty id routes by service name while exactly one transport offers it, and is an ambiguity error once more than one does (see [`networking.md`](networking.md#routing-a-transports-typed-rpcs-extensionrpc)).

Both EconetService.* AUN methods and the new AunService.* methods exist concurrently for backward compatibility; the EconetService duplicates carry `DEPRECATED` comments and will be removed in a follow-up coordinated with Python and macOS Swift client updates.

### Core library prerequisites

Before the gRPC service can be implemented, these accessors are needed:

- `EconetSocket::backend()` — returns `NetworkBackend*` for peer management
- `EconetSocket::aun_mode()` — query whether AUN mode is active
- `AunBackend::list_peers()` — enumerate the peer table (returns `vector<PeerInfo>`)
- `PiconetBackend::config()` — exposes the device path for status reporting (already in place)

### Proto outline

```
service EconetService {
    GetEconetStatus     — hardware state, ADLC registers, handshake stage, connection
    EnableEconet        — fit Econet hardware (station ID, AUN port, AUN mode)
    DisableEconet       — remove Econet hardware
    AddPeer             — add Econet address <-> UDP endpoint mapping
    RemovePeer          — remove peer by Econet address
    ListPeers           — enumerate configured peers
    SubscribeEconetEvents — stream frame, handshake, and connection events
}
```

Key messages:
- `GetEconetStatusResponse` includes nested `AdlcStatus` (CR1-4, SR1-2, FIFO state) and `HandshakeStatus` (stage, flag fill)
- `EconetEvent` carries event type, timestamp, and optional frame info / peer info / handshake stage
- `EconetFrameInfo` carries frame type, addresses, port, control byte, data length, and (possibly truncated) payload

### Key files

- `src/service/proto/econet.proto` — new proto definition
- `src/service/include/beebium/service/EconetService.hpp` — template service implementation
- `src/service/include/beebium/service/Server.hpp` — registration
- `src/core/include/beebium/econet/EconetSocket.hpp` — add accessors
- `src/core/include/beebium/econet/AunBackend.hpp` — add `list_peers()`
- `tests/test_grpc_econet.cpp` — service tests

---

## Phase 3: Service Discovery Metadata

Add Econet TXT records to the mDNS advertisement so discovery clients can see which machines have Econet fitted and their station numbers.

### TXT records

When Econet is enabled: `econet_station=N`

### Key files

- `src/service/include/beebium/service/SystemService.hpp` — add TXT record in `SetAdvertisement`
- `clients/beebium-python-client/src/beebium/discovery.py` — parse `econet_station` from TXT records

---

## Phase 4: Python Client

The Python client splits along the same line as the gRPC services: an
`Econet` wrapper around the transport-agnostic `EconetService`, and an
`Aun` wrapper around the AUN-specific `AunService`. Other transports
(Piconet) would get their own wrappers if/when transport-specific
RPCs are added.

### Wrapper class outline

```python
class Econet:                              # wraps EconetService
    status -> EconetStatus                 # generic: enabled, station_id, ADLC
    is_enabled -> bool
    station_id -> int
    enable(station_id, aun_mode)           # transport-agnostic
    disable()
    events() -> Iterator[EconetEvent]
    start_background_events(callback) -> EventStreamHandle


class Aun:                                  # wraps AunService
    status -> AunStatus                     # AUN-specific: port, peers, link
    peers -> list[PeerInfo]
    set_connected(connected: bool)
    add_peer(net, station, ip_address, port)
    remove_peer(net, station)
```

### Key files

- `clients/beebium-python-client/src/beebium/econet.py` — new wrapper class
- `clients/beebium-python-client/src/beebium/connection.py` — add `econet_stub`
- `clients/beebium-python-client/src/beebium/client.py` — add `econet` property
- `clients/beebium-python-client/src/beebium/__init__.py` — export new classes
- `clients/beebium-python-client/tests/test_econet.py` — mock stub tests

---

## Phase 5: macOS Client

### UI integration

The macOS sidebar already has a `.network` mode (case 8 in `SidebarMode.swift`) with the "network" SF Symbol icon. This is the natural home for Econet controls.

### Key files

- `clients/macos/Beebium/Beebium/EconetClient.swift` — gRPC client wrapper (`@MainActor`, `ObservableObject`, `Disconnectable`)
- `clients/macos/Beebium/Beebium/NetworkModeView.swift` — sidebar content for `.network` mode
- `clients/macos/Beebium/Beebium/ContentView.swift` — create and register `EconetClient`
- `clients/macos/Beebium/Beebium/SidebarModeContent.swift` — route `.network` to `NetworkModeView`
- `clients/macos/Beebium/Beebium/Configuration/` — Econet section in configuration editor (preset UI)
- Generated Swift proto stubs from `econet.proto`
