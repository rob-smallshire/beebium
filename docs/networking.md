# Econet and AUN Networking Support

This document covers the design, research, and implementation of Econet and AUN (Acorn Universal Networking) support in Beebium. The core networking implementation (MC68B54 ADLC emulation, EconetSocket, FourWayHandshake) is complete, with the wire-side transport pluggable through the `EconetTransportExtension` extension point: AUN ships as a built-in extension, Piconet as a discoverable plugin, and `TestBackend` as the in-process test double. The architecture has been validated end-to-end with a real BBC Microcomputer talking via real Econet to a Beebium-emulated Level 3 File Server. See `docs/econet-integration.md` for the remaining integration work (presets, gRPC, service discovery, clients).

## Overview

Econet is Acorn's local area networking system, introduced in 1981. It connects BBC Micros (and later Acorn machines) allowing shared access to file servers, printers, and inter-machine communication. The hardware is based on the Motorola MC68B54 Advanced Data Link Controller (ADLC).

AUN (Acorn Universal Networking) encapsulates Econet protocols over TCP/IP, originally developed for RISC OS but now used to bridge vintage Econet hardware with modern networks.

[Piconet](https://github.com/jprayner/piconet) is a USB device that exposes a genuine MC68B54 ADLC connected to a real Econet socket, controlled via a text protocol over USB-CDC. Beebium's `PiconetBackend` lets an emulated BBC participate in a physical Econet network, indistinguishable at the protocol level from a real Acorn machine.

### Goals for Beebium

1. **Emulate the MC68B54 ADLC** at the hardware level — **Done.** Cycle-accurate on the E-clock domain, with PSE, stored/present status, and full register semantics. See `Mc6854.hpp`.
2. **Support AUN protocol** for network connectivity — **Done.** `AunBackend` handles UDP transport; `FourWayHandshake` bridges AUN's two-way protocol to the four-way handshake that NFS ROMs expect.
3. **Enable connectivity with real Econet hardware** via the Piconet USB device — **Done.** `PiconetBackend` drives a real Piconet on `/dev/tty.usbmodem*`. Validated against a PiEconetBridge-hosted fileserver over a real Econet wire, and end-to-end against a real BBC Microcomputer talking to a Beebium-hosted Level 3 File Server. See `docs/discussion/piconet-feasibility.md` for the design and `docs/discussion/piconet-upstream-issues.md` for upstream-side findings.
4. **Support NFS/ANFS ROMs** for file server access — **Done.** NFS 3.34 works correctly, including boot messages, `*I AM`, `*CAT`, `*DATE`, and file operations against a real Acorn Level 3 Fileserver.

## Network Transport Backends

Beebium decouples the emulated ADLC from the underlying transport via the `NetworkBackend` abstraction (`src/core/include/beebium/econet/NetworkBackend.hpp`). Concrete backends are produced by `EconetTransportExtension` instances (`src/core/include/beebium/extension/EconetTransportExtension.hpp`) — see "Econet Transport Extensions" below for the extension-point design.

| Backend | CLI flag | Transport | Use case |
|---|---|---|---|
| `AunBackend` (built-in `aun` extension) | `--aun [port=<n>][:net=<n>][:map=<net.stn@ip@port>]...` (default `port=32768:net=0`) | UDP/IP, AUN-encapsulated | Talk to other Beebium instances, BeebEm, PiEconetBridge, or any AUN-speaking peer over IP. Auto-discovers other `_aun._udp` peers on the LAN; manual `map=` is no longer required between Beebium instances on the same network. |
| `PiconetBackend` (`piconet` plugin extension) | `--piconet device_path=<path>` | USB-CDC serial to a Piconet board | Talk to real BBCs / Acorn fileservers / printers over a real Econet wire (POSIX-only) |
| `TestBackend` | selected automatically by `--aun port=none` or by passing `--station <n>` with no transport flag | In-process; no I/O | Hardware fitted, no transport — NFS ROM sees "No Clock". Also the test double for unit tests. |

**Mutual exclusion:** `--piconet` and `--aun` cannot both be specified. The choice of transport is established at server startup; runtime swapping is not supported. BBC Micro / Master / Master Compact machines accept at most one transport. (The transport registry is intentionally non-singleton so future machine types like the Acorn Econet Bridge — two memory-mapped ADLCs — could hold two; per-machine cardinality is enforced at machine-setup time.)

**Configuration via preset JSON** mirrors the CLI through a single `econet.transport` object that names the transport extension and carries its parameters:

```json
"econet": {
  "station": 32,
  "transport": {
    "name": "aun",
    "parameters": { "port": "32768", "map": "0.254@127.0.0.1@32769" }
  }
}
```

`name` selects the extension (`aun` or `piconet`); `parameters` is a flat key/value map. The legacy preset keys `econet.aun_port`, `econet.aun_map`, and `econet.piconet.device_path` were removed; presets that still use them fail to load with a message pointing at the new shape.

### Inner separators in `--aun map=`

The extension argument parser tokenises `--<extension> a:b:c` on `:`, which conflicts with the obvious `0.254:127.0.0.1:32768` peer-map format. AUN's `map=` parameter therefore uses `@` as the inner separator: `map=0.254@127.0.0.1@32768`. Repeated `map=` tokens accumulate (the `is_list` schema flag) so multiple peers can be specified on one CLI invocation:

```
--aun port=32768:map=0.254@127.0.0.1@32769:map=0.253@127.0.0.1@32770
```

`@` is chosen because it is shell-safe across bash, zsh, fish, cmd.exe, and PowerShell -- no quoting is required -- and collides with neither `.` (used inside `net.stn` and IPv4 addresses) nor `:` (the top-level argument separator). The `is_list` parser plumbing now preserves repeated tokens as an opaque vector, so each extension is free to pick whatever inner separator reads best for its data; `@` is a convention for AUN, not a framework rule.

In preset files, `map` accepts either a single string or a JSON array of strings:

```json
"parameters": {
  "port": "32768",
  "map": ["0.254@127.0.0.1@32769", "0.253@127.0.0.1@32770"]
}
```

A single-string form (`"map": "0.254@127.0.0.1@32769"`) remains supported for backwards compatibility and is normalised to a one-element list at load time.

### Local Econet net number (`--aun net=N`)

The `net` parameter (default `0`, valid range `0..127`) declares which Econet net number this station belongs to. AUN does not enforce a flat-net topology — each station's per-station peer table *is* its routing table, and there's no central authority assigning nets — so a station that wants to participate in a multi-net deployment must self-declare which net it's on.

```
--aun port=32768:net=3
```

The high bit (128..255) is reserved by the Acorn bridge protocol and is rejected. Beebium falls back to `net=0` with a warning rather than failing; passing nothing is equivalent to `net=0`.

**Net 0 semantics.** BBC software addresses local-segment peers with `dest_net=0` ("this segment, don't route") on the wire, regardless of what net number the segment actually carries. `AunBackend` translates `dest_net=0` to the configured `local_net` before consulting the peer table, and on the receive side translates `src_net == local_net` back to `0` so the BBC sees frames in the form it expects. Cross-net frames (different absolute nets) pass through both translations unchanged. This means:

- `--aun net=0` is the historical default and is fully wire-compatible with any AUN peer that uses the flat-cloud convention.
- `--aun net=N` (non-zero) is useful for testing multi-net scenarios locally and for participating in deployments where a bridge has assigned a non-zero net to this segment.
- Map entries (whether operator-configured via `map=` or auto-discovered via mDNS) can use any net 0..127; only the `dest_net=0` BBC convention triggers the translation.

### AUN peer discovery via mDNS

Beebium publishes a `_aun._udp` DNS-SD announcement when its AUN transport binds, and subscribes to the same service type on the LAN. Discovered peers are added to the routing table automatically as `Discovered` entries; operator-configured peers (from `--aun map=` or `AunService::AddPeer`) always take precedence — a discovered announcement that claims an `(net, stn)` already pinned by the operator is silently ignored.

This means **Beebium-to-Beebium AUN works with no `map=` configuration on the same LAN**: launch two servers with `--aun port=32768 --station 1` and `--aun port=32769 --station 254` and they'll find each other within a couple of seconds (`mDNS resolve + getaddrinfo` round-trip).

The TXT record schema, the rationale for the vendor-neutral service type, and the choice of `net=` as a mandatory field are documented in [`docs/discussion/aun-mdns-peer-discovery.md`](discussion/aun-mdns-peer-discovery.md). Other AUN implementations (BeebEm, PiEconetBridge, real Acorn hardware) are explicitly invited to adopt the same schema; nothing in it is Beebium-specific.

**Platform support today:**

- macOS — full support via Bonjour (`dns_sd.h`): both advertises and browses.
- Linux — full bidirectional discovery via `AvahiAdvertiser` and `AvahiBrowser` (libavahi-client, loaded at runtime with `dlopen`), so a Linux server both publishes and discovers `_aun._udp` peers with no manual `map=` needed. Requires a running `avahi-daemon`; where Avahi is absent both sides degrade silently to a no-op (advertiser) / unavailable (browser).
- Windows — full bidirectional discovery (publishes and discovers `_aun._udp` peers, no manual `map=` needed), with a provider chosen at run time:
  - **Apple Bonjour** (`dnssd.dll`) when it is installed (iTunes, Adobe apps, etc. bundle it). Bonjour takes over the mDNS responder (UDP 5353), so where present it is preferred; it exposes the same `dns_sd.h` API the macOS advertiser/browser use, resolved at run time via `GetProcAddress` (no Bonjour SDK needed to build).
  - **Native `windns.h`** (`DnsServiceRegister` / `DnsServiceBrowse` / `DnsServiceResolve`) otherwise. Requires Windows 10 1903+; always present, so there is always a provider.

  Note: on a machine with Bonjour installed the native path may be unable to register (Bonjour owns 5353), which is exactly why the Bonjour provider is preferred when available.

**Future improvements:**

- An optional `--aun no-discovery` switch (or `BEEBIUM_AUN_DISCOVERY=off`) for environments where the operator wants to opt out of mDNS entirely (corporate networks, paranoid users) without disabling the AUN transport.
- A future DSCP (Discovery Service Coordination Protocol, name TBD) layer on top of mDNS could let peers negotiate richer capability information — e.g. supported AUN extensions, machine model, fileserver hosting status — without requiring every consumer to talk gRPC. mDNS shipped first because it's independently useful; DSCP is the natural next step if richer per-peer metadata is wanted.
- Bridge-as-bridge announcements via a separate `_acorn-bridge._udp` service type, once Beebium grows a machine type with two ADLCs (the Acorn Econet Bridge). This is reserved but not implemented; only `_aun._udp` is published today.

### `FourWayHandshake` is always in the path

All three backends operate behind the `FourWayHandshake` decorator (`aun_mode = true` is the default for production use). Econet's wire protocol is a four-way handshake (scout / scout-ack / data / data-ack). AUN's UDP protocol is two-way (Unicast / Ack). Even Piconet is *atomic* from the host's perspective — the firmware completes the wire handshake before reporting the result. `FourWayHandshake` synthesises the missing scout-ack and final-ack frames locally so the NFS ROM sees the timing it expects regardless of the transport underneath.

### When to use which

- **Other Beebium emulators on the same host or LAN:** `AunBackend`. mDNS discovery handles the peer-table population automatically (no manual `--aun map=` needed) on all three platforms (macOS, Windows, Linux); fall back to explicit `map=` for hermetic test runs, for hosts without a working mDNS responder installed, or for WAN deployments.
- **A real BBC, Acorn fileserver, or other Econet peripheral:** `PiconetBackend` with a Piconet device on the wire. The wire's clock generator and termination must be present (the Piconet is a participant, not a clock source).
- **Testing only:** `TestBackend` (or the test fakes `MockPiconetSerial`, `FakePiconetDevice`, `FakePiconetDeviceOnPty`, `AunBridgePiconetDevice` in `tests/piconet/`). The Piconet integration's test stack is described in `docs/discussion/piconet-feasibility.md` ("Testing Strategy" section).

### Background: known gaps

`PiconetBackend` does not support **inbound immediate operations other than MachinePeek** — the firmware's host-driven REPLY path was abandoned upstream and Piconet handles MachinePeek inline with a canned response. Standard fileserver and printer traffic is unaffected. Documented in detail in `docs/discussion/piconet-feasibility.md` ("Immediate Operations Limitation").

### Emulation speed and real-time peers

Beebium can run the emulated machine faster or slower than real time (the
Processor panel's speed control; `SystemService.SetSpeedMultiplier`). "Speed"
here is purely the ratio at which **emulated time** advances relative to **wall
time** — wall time itself never changes rate. A self-contained machine at 2x
simply experiences two seconds of its own time (CPU, VIA timers, the emulated
clock) per wall second, and is internally consistent; there is no problem.

Networking is the exception, because an Econet peer lives in **our** wall-clock
universe, and the guest's Econet protocol timing (the four-way handshake and its
timeouts, run by the NFS ROM on the emulated CPU) is therefore in **emulated**
time. The moment those two clocks diverge, the coupling can break — and whether
it does depends on the transport:

- **AUN** has no Econet line clock; it is UDP datagrams. The only coupling is
  soft: the guest's emulated-time protocol timeouts versus wall-clock UDP
  latency. A moderate speed-up merely tightens those timeouts in wall terms (a
  200ms emulated timeout is 100ms of wall at 2x — still vast next to LAN
  latency); it only bites at extreme speed, and then it fails *gracefully*
  ("Net Error", retry), not corruptingly. Slowdown is entirely benign. **AUN
  needs no gating.**

- **Piconet** bridges to a **real physical Econet with real stations running in
  real time**. The serial byte/baud mismatch between Beebium and the Piconet
  device is absorbed by buffering, but a byte buffer cannot re-pace
  *protocol-level* timing: the guest's emulated-time handshake has to interleave
  with real stations responding in wall time, and that coupling is immovable.
  Both directions break it, symmetrically — faster than 1x and the guest's own
  timeouts abandon real peers too soon; slower than 1x and real peers time the
  guest out. Only **exactly 1x** keeps both happy.

The policy follows from this: a transport declares whether it must run at real
time via `EconetTransportExtension::requires_real_time_pacing()` (AUN → `false`,
Piconet → `true`). When a transport that requires it is active and the speed is
not 1x, **that transport is gated** — it stops carrying traffic (the guest sees
a quiet network and recovers at 1x) rather than silently corrupting it. This is
enforced **server-side** (the server owns pacing and knows the active
transports), so programmatic clients are bound by it too, not just the GUI;
clients can observe the gating state and surface *why* a transport is paused.
Concretely: an `EconetSocket` carrying a real-time transport inserts a
`SpeedGate` decorator above the backend (alongside `FourWayHandshake`) that
severs the wire while gated, and the gating state is reported on
`GetEconetStatusResponse` (`requires_real_time`, `gated_by_speed`).

> **Dual-ADLC bridge note.** This gating is currently *socket-level*: one
> transport per `EconetSocket`, one `gated_by_speed` flag. The future
> Acorn-Econet-Bridge machine type would have two ADLCs / two transports, so the
> speed gating and the status fields exposing it will need to become
> per-transport. Revisit this design decision when that machine lands.

## Econet Transport Extensions

Beebium's transports are *extensions*, dispatched through the same machinery that handles peripheral extensions (acorn-rtc, acorn-scsi, etc.). The split between built-in and plugin extensions and the C++ class hierarchy that supports it:

- `Extension` (`src/core/include/beebium/extension/Extension.hpp`) — common base. Holds the manifest and instance config; provides `name()`, `description()`, `id()`, `label()`, `config_value()`. No lifecycle methods.
- `EconetTransportExtension : Extension` (`src/core/include/beebium/extension/EconetTransportExtension.hpp`) — the transport extension-point interface. Three virtual hooks:
  - `std::unique_ptr<NetworkBackend> create_backend(uint8_t station)` — produce the wire-side backend.
  - `void on_station_id_changed(uint8_t)` — propagate gRPC `SetStationId` updates to transports that need to inform a downstream device (Piconet's `SET_STATION` command).
  - `std::vector<grpc::Service*> grpc_services()` — optional gRPC services the transport contributes (AUN extension exposes `AunService` here).
- `PeripheralExtension : Extension` — the parallel base for hardware-port peripherals (1MHz bus, User VIA, Tube). Unchanged by the transport refactor; lifecycle is `attaches_to / provides / init(ExtensionContext&) / shutdown / grpc_services`.

### Built-in vs plugin

Each extension is either compiled into the server (built-in) or loaded from an on-disk shared library at startup (plugin). A small registry table at `src/server/include/beebium/server/BuiltinExtensions.hpp` lists the built-in extensions (manifest + factory function); the same `--<cli-name>` dispatch resolves built-ins first and falls back to scanned plugin manifests.

| Extension | Kind | Lives in | Loaded from |
|---|---|---|---|
| `aun` (AUN UDP) | built-in | `src/extensions/aun/` | linked into `beebium-model-b` etc. |
| `piconet` (USB-CDC bridge) | plugin | `src/extensions/piconet/` | `<extension-dir>/piconet/piconet.{so,dylib}` + `manifest.json`, POSIX-only |
| `acorn-65c02-coprocessor` (Tube) | built-in | `src/extensions/acorn-65c02-coprocessor/` | linked into the server |
| `acorn-scsi`, `acorn-rtc`, `scsi-hard-disc`, `test-scratch-ram` | plugins | `src/extensions/<name>/` | dlopen'd from the extension directory |

### Manifest

Every extension carries a manifest declaring its CLI name, parameter schema, and `extension_kind` (`"peripheral"` or `"econet-transport"`). For plugin extensions the manifest is a `manifest.json` file alongside the shared library; for built-ins it's constructed programmatically in `BuiltinExtensions::entries()`. Example for the AUN built-in:

```json
{
  "name": "aun",
  "description": "AUN (Acorn Universal Networking) UDP econet transport",
  "cli": "aun",
  "extension_kind": "econet-transport",
  "parameters": [
    {"key": "port", "type": "string",
     "description": "UDP port to bind (decimal, or 'none' to disable)",
     "default_value": "32768"},
    {"key": "map", "type": "string", "is_list": true,
     "description": "Peer entry 'net.stn@ip@port' (repeatable)"}
  ]
}
```

The CLI parser uses `cli_name` to recognise `--aun ...`; the parameter schema drives validation of the colon-separated argument string. `parameters[*].is_list = true` accumulates repeated `key=value` tokens into a `std::vector<std::string>` of raw values; the parser does not tokenise their contents, so each extension picks whatever inner separator suits its data.

### Lifecycle

For an econet-transport extension the order is:

1. `ServerMain` parses CLI flags, building a list of `ExtensionInstance{name, config}` records.
2. Plugin manifests are scanned from the extension directory.
3. The `EconetTransportRegistry` is populated by walking the instance list — entries whose manifest `extension_kind == "econet-transport"` are constructed (via `BuiltinExtensions::find()` for built-ins or `PluginLoader::load_extension()` for plugins) and added to the registry. This happens **before `machine.reset()`** so the BBC's reset routine sees a configured ADLC.
4. `install_econet` queries the registry: if it holds a transport, it calls `transport->create_backend(station)` and hands the result to `EconetSocket::enable()`. Otherwise an internally-disconnected `TestBackend` is installed (NFS sees "No Clock").
5. After the gRPC server starts, the transport registry's `collect_grpc_services()` contributes its services (e.g. `AunService`) alongside the peripheral extensions' services.

The same code path serves both built-in (AUN) and plugin (Piconet) transports — the only difference is whether `BuiltinExtensions::find()` or `PluginLoader::load_extension()` constructs the instance.

### Network sidebar (macOS GUI)

The macOS frontend's Network sidebar (sidebar mode 8) is split into a transport-agnostic header and a per-transport panel. The split mirrors the architectural distinction between concerns common to every Econet transport (link state, station number) and concerns specific to one transport (AUN's UDP port and peer table; Piconet's USB device and firmware mode).

**Transport-agnostic header** (hardcoded SwiftUI in `NetworkModeView`, driven by `EconetService.GetEconetStatus`):

- **Connection** — "Connected" / "Disconnected", driven by `EconetService.GetEconetStatus.connected`. For AUN this reflects the cable-simulation state (toggled via the panel's Connect/Disconnect button); for Piconet it reflects `is_serial_open() && mode == LISTEN`. Same row, same widget, same meaning across transports — "is the BBC actually in two-way comms with the wire?"
- **Econet Station** — current station number with a pencil-edit affordance opening a popover (`SetStationId` via `EconetService`). Transport-agnostic.
- **Speed-gating banner** — a yellow `exclamationmark.triangle` warning shown at the top of the panel when `GetEconetStatus.gated_by_speed` is true: the active transport requires real-time emulation (`requires_real_time`) but the emulation speed is not 1x, so its traffic has been severed (see "Emulation speed and real-time peers" above). The banner reuses the existing connection-error idiom and directs the user to the Processor panel to restore 1x speed. It is transport-agnostic — any transport reporting `requires_real_time` triggers it — but in practice only Piconet does.

**Per-transport panel** (server-pushed `View` rendered by `ExtensionPanelView`, driven by whichever transport extension is active):

- **AUN** (`AunUi`):
  - `Connect` / `Disconnect` Button — toggles `AunBackend::set_connected`. Label flips with the current state.
  - `Listening on UDP port N` Label.
  - `Peers` group — one Label per configured peer (`net.stn  ip:port`), or "No peer stations configured" when empty.

- **Piconet** (`PiconetUi`):
  - Device path Label (`Device: /dev/tty.usbmodem...`).
  - Indicator — "Adapter responsive" (green) when the USB serial port is open; "Cannot open device: <errno>" (red) if the open failed at startup; "Adapter offline" (red) after a successful open followed by hot-unplug.
  - `Enable` / `Disable` Button — toggles the firmware between LISTEN and STOP. Suppressed when the serial port is closed (no firmware to drive).

The transport panel is rendered through the **Extension UI framework** (see [`docs/discussion/extension-ui-architecture.md`](discussion/extension-ui-architecture.md)). The framework streams a typed control tree from the server to the client; the macOS renderer walks the tree once per push and produces native widgets. Adding a control to the AUN or Piconet panel requires server-side code only — the macOS app needs no changes to surface a new control type that's already in the seven-control vocabulary.

The transport panel does not provide manual peer management for AUN. The Add/Remove peer affordance was designed but not built — the long-term direction is centralised station assignment via DSCP plus mDNS/Bonjour peer discovery (see `docs/discussion/dynamic-station-config-protocol.md`). Scripts can still manipulate peers via `AunService.AddPeer` / `RemovePeer` / `SetConnected` / `ListPeers` (typed RPCs that stay in place — see `feedback_extension_multi_api.md` in project memory for the principle that extensions can expose multiple APIs for different audiences).

When the device is unplugged mid-session, Piconet correctly transitions to the offline state but does not auto-reconnect when the device returns. Re-attachment is tracked in [`docs/discussion/piconet-device-discovery.md`](discussion/piconet-device-discovery.md) for a future focused branch.

## Hardware Architecture

### Memory Map

| Address | Function | Access |
|---------|----------|--------|
| &FE18 | Station ID register | Read only |
| &FEA0 | Control Register 1 / Status Register 1 | Write / Read |
| &FEA1 | Control Registers 2,3 / Status Register 2 | Write / Read |
| &FEA2 | Transmit FIFO (Frame Continue) / Receive FIFO | Write / Read |
| &FEA3 | Transmit FIFO (Frame Terminate) / Receive FIFO | Write / Read |

The station ID at &FE18 is set by hardware links (S11) on the Model B and returns a value 0-255. Reading this register also triggers INTOFF (disables NMI from the ADLC).

### Station ID Across Machine Types

The station number is obtained differently depending on the machine:

| Aspect | Model B / B+ | Master 128 | Master Compact |
|--------|-------------|------------|----------------|
| **Storage** | Binary links (S11) on PCB | CMOS RAM byte 0x0E (MC146818 RTC) | EEPROM |
| **Address** | &FE18 hardware register | CMOS RAM (read via OSBYTE) | EEPROM (read via OSBYTE) |
| **Persistence** | Fixed at manufacture | Battery-backed, survives power-off | Non-volatile |
| **Runtime modification** | No (hardware links) | Yes (`*SETSTATION`) | Yes (`*SET` network utility) |
| **INTOFF address** | &FE18 | &FE38 | &FE38 |
| **INTON address** | &FE20 (Video ULA range) | &FE3C | &FE3C |

**Model B / B+:** Station number is hardwired by binary links. The NFS ROM reads it from the &FE18 register (which also triggers INTOFF). The number cannot be changed without physically modifying the links.

**Master 128:** Station number stored in CMOS RAM at byte offset 0x0E of the MC146818 Real-Time Clock chip (which provides 50 bytes of user RAM alongside its timekeeping registers, addresses 0x0E-0x3F). The ANFS ROM reads it from CMOS at boot. The byte is protected against normal OSBYTE 162 writes — it must be set via `*SETSTATION` (a network utility command, typically run from a file server), or by directly programming the 6522 VIA chip at &FE40-&FE43 which drives the RTC chip's read/write strobes.

**Master Compact:** Station number stored in EEPROM rather than CMOS RAM. Can only be set via the `*SET` network utility (direct VIA access doesn't work). EEPROM has limited write endurance (~10,000 cycles). Econet connectivity requires fitting the optional MC68B54 daughter board and an ANFS ROM.

**CMOS layout** (Master 128, from BeebEm's `Rtc.cpp`):
```
Byte 0x0E: Econet station number        *SETSTATION nnn
Byte 0x0F: File server station number    *CONFIGURE FS nnn
Byte 0x10: File server network number    *CONFIGURE FS nnn.sss
Byte 0x11: Printer server station number *CONFIGURE PS nnn
Byte 0x12: Printer server network number *CONFIGURE PS nnn.sss
```

**Implication for Beebium:** For Model B (our initial implementation), the station number comes from `--station N` on the command line and is returned by the &FE18 register. For future Master support, the station number would instead be stored in the emulated CMOS RAM, and the &FE18 register would not exist (the station ID register is at a different address, &FE38, with different semantics).

### MC68B54 ADLC Registers

The ADLC has four control registers (CR1-CR4) and two status registers (SR1, SR2), plus transmit and receive FIFOs.

**Control Register 1 (CR1):** (RS1=0, RS0=0, R/W=0)
- Bit 0: Address Control (AC) - selects CR2 vs CR3/CR4 for writes to RS1=0,RS0=1
- Bit 1: Receiver Interrupt Enable (RIE) - 1=enable RX interrupts
- Bit 2: Transmitter Interrupt Enable (TIE) - 1=enable TX interrupts
- Bit 3: RDSR Mode - DMA mode for receiver, inhibits RDA-caused IRQ
- Bit 4: TDSR Mode - DMA mode for transmitter, inhibits TDRA-caused IRQ
- Bit 5: Rx Frame Discontinue - discards current frame, auto-resets when frame discarded
- Bit 6: Receiver Reset (RxRS) - 1=hold receiver in reset, must write 0 to release
- Bit 7: Transmitter Reset (TxRS) - 1=hold transmitter in reset, transmits marks (1s)

**Status Register 1 (SR1):** (RS1=0, RS0=0, R/W=1)
- Bit 0: Receiver Data Available (RDA) - mirrors SR2 bit 7 for convenience
- Bit 1: Status #2 Read Request (S2RQ) - OR of all SR2 stored conditions (except RDA)
- Bit 2: Loop Status - 1 when in "On-Loop" condition (Loop Mode only)
- Bit 3: Flag Detected (FD) - flag received (if FD Enable set), cleared by CLR Rx Status
- Bit 4: Clear To Send (CTS) - positive edge stored, cleared by CLR Tx Status
- Bit 5: Transmit Underrun (TxU) - TX ran out of data, frame aborted
- Bit 6: TDRA/Frame Complete - TX FIFO available OR frame complete (selectable)
- Bit 7: Interrupt Request (IRQ) - 1 when IRQ output is active (low)

**Status Register 2 (SR2):** (RS1=0, RS0=1, R/W=1)
- Bit 0: Address Present (AP) - frame boundary indicator; an address octet is available in the Rx FIFO. In Extended Addressing Mode, AP continues to indicate addresses until the address field is complete. Cleared by reading data or by Rx Reset.
- Bit 1: Frame Valid (FV) - frame complete with no error. Set when the last data byte of a frame is transferred into the last FIFO location. Once FV is set, the ADLC stops further data transfer into the last FIFO location (preventing mixing of two frames). Cleared by CLR Rx Status or Rx Reset.
- Bit 2: Inactive Idle Received (Rx Idle) - 15+ consecutive 1s received. Stored in status register and causes interrupt. The status bit is the logical OR of the receiver idling detector (which continues to reflect idling until a "0" is received) and the stored inactive idle condition. Cleared by CLR Rx Status.
- Bit 3: Abort Received (RxABT) - 7+ consecutive 1s received. Has no meaning under out-of-frame conditions; no interrupt or storing occurs unless a flag has been detected prior to the abort. An in-frame abort is stored and causes IRQ. The status bit is the logical OR of the stored condition and the Rx abort detect logic. Cleared by CLR Rx Status. The stored abort condition is also cleared by Rx Reset.
- Bit 4: FCS/Invalid Frame Error (ERR) - CRC error or short frame (frame does not have complete Address and Control fields). When ERR is set instead of FV, other functions (frame boundary indication, control function) are exactly the same as for FV. Cleared by CLR Rx Status.
- Bit 5: Data Carrier Detect (DCD) - a positive transition on the DCD input is stored in the status register and causes IRQ. Cleared by CLR Rx Status or Rx Reset. The status bit is the logical OR of the stored condition and the present DCD input state. Note: high DCD resets the receiver section.
- Bit 6: Receiver Overrun (OVRN) - receiver data transferred into Rx FIFO when it is full, resulting in data loss. Cleared by CLR Rx Status or Rx Reset. Continued overrunning only destroys data in the first FIFO register.
- Bit 7: Receiver Data Available (RDA) - receiver data can be read from the Rx FIFO. In prioritised status mode (PSE=1), indicates non-address and non-last data available. In 1-Byte Transfer Mode, RDA high means the last register of the FIFO causes RDA to be high. In 2-Byte Transfer Mode, RDA high indicates the last two registers are full. RDA is reset automatically when data is not available.

**Status Priority (Prioritized Status Mode):**
When PSE=1 in CR2, status bits are prioritized - higher priority bits suppress lower ones.
Priority order (highest to lowest): IRQ > TDRA/FC > TxU > CTS > FD > S2RQ > RDA.
Programming tip: Test lowest priority (most frequent) conditions first.

**Stored vs Present Status:**
Certain SR2 status bits represent the logical OR of a *stored* (latched) condition and the *present* (live) pin/input state:

| Bit | Stored condition | Present condition |
|-----|-----------------|-------------------|
| DCD (b5) | Positive edge latched | Current DCD input level |
| RxABT (b3) | In-frame abort latched | Rx abort detect logic output |
| Rx Idle (b2) | Inactive idle latched | Receiver idling detector output |

Similarly in SR1: CTS (b4) latches a positive edge on CTS, while the present CTS input can still assert. Clearing the stored condition (via Clear Rx Status or Clear Tx Status) reveals the present condition — if the input is still asserted, the status bit remains set. This means software must handle the case where clearing a status bit doesn't actually clear it because the underlying condition persists.

**Control Register 2 (CR2):** (RS1=0, RS0=1, R/W=0, AC=0)
- Bit 0: Prioritized Status Enable (PSE) - enables status bit suppression
- Bit 1: 2-Byte/1-Byte Transfer - controls when TDRA/RDA indicate availability
- Bit 2: Flag/Mark Idle Select - 1=flags, 0=mark idle during transmit idle
- Bit 3: Frame Complete/TDRA Select - selects meaning of SR1 bit 6
- Bit 4: Transmit Last Data - signals last byte, auto-clears
- Bit 5: Clear Receiver Status - clears stored RX status bits (except AP, RDA)
- Bit 6: Clear Transmitter Status - clears stored TX status bits (except TDRA)
- Bit 7: RTS Control - 1=RTS output low (active)

**Control Register 3 (CR3):** (RS1=0, RS0=1, R/W=0, AC=1)
- Bit 0: Logical Control Field Select (LCF)
- Bit 1: Extended Control Field Select (Cex)
- Bit 2: Auto Address Extend Mode (Aex)
- Bit 3: 01/11 Idle
- Bit 4: Flag Detect Status Enable (FDSE)
- Bit 5: Loop/Non-Loop Mode
- Bit 6: Go Active on Poll/Test (GAP/TST)
- Bit 7: Loop On-line Control/DTR (LOC/DTR)

**Control Register 4 (CR4):** (RS1=1, RS0=1, R/W=0, AC=1)
- Bit 0: Double Flag/Single Flag Interframe Control
- Bits 1-2: Word Length Select Transmit (5-8 bits)
- Bits 3-4: Word Length Select Receive (5-8 bits)
- Bit 5: Transmit Abort - transmits abort sequence
- Bit 6: Abort Extend - extends abort to 16 bits
- Bit 7: NRZI/NRZ Select

### FIFO Operation

The ADLC has 3-byte FIFOs for both transmit and receive. Data transfers between registers on both phases of the E clock.

**Transmit FIFO:**
- Write to "Frame Continue" address (&FEA2): sets frame boundary pointer
- Write to "Frame Terminate" address (&FEA3): resets frame boundary pointer
- When negative transition detected at third FIFO location, transmitter appends FCS and closing flag

**Receive FIFO:**
- Address Present (AP) bit indicates address byte available
- Frame Valid (FV) set when last byte enters last FIFO location
- Once FV is set, further data transfer to last location is blocked until status cleared

### Programming Considerations (from Motorola Datasheet)

The MC68B54 datasheet includes several programming notes that are important for correct emulation:

1. **Status priority testing**: When prioritised status mode (PSE) is used, test the lowest priority conditions first, as these are the most frequently occurring and most likely to exist when the processor is interrupted.

2. **Stored vs Present status clearing**: In prioritised mode, a status condition must be read before it can be cleared. Clearing a higher-priority condition might result in a new IRQ from a lower-priority condition whose status was previously suppressed. This guarantees that status conditions are never inadvertently cleared without software having seen them.

3. **Rx FIFO clearing**: An Rx Reset clears all three Rx FIFO bytes. However, the FIFO may contain data from two different frames when an abort or DCD failure occurs mid-frame. Data from a previously closed frame (one whose closing flag has been received) will not be destroyed.

4. **Servicing Rx FIFO in 2-Byte Mode**: The procedure for reading the last bytes of data is the same regardless of whether the frame contains an even or odd number of bytes. Continue to read 2 bytes until an end-of-frame status (FV or ERR) occurs. When this occurs, indicating the last byte has been read or is ready to be read, switch temporarily to 1-byte mode with non-prioritised status (control register 2) to check whether a 1-byte read is indicated.

5. **Frame Complete Status and RTS Release**: In many cases (particularly with modems), a delay is required for releasing RTS after frame completion. An 8-bit or 16-bit delay can be added to the ADLC RTS output at the end of a transmission. After frame complete status goes high, write "1" into the Abt control bit (and Abt Extend bit if a 16-bit delay is required). After the Abt control bit is set, write "0" into the RTS control bit. The transmitter will transmit eight or sixteen "1"s and the RTS output will then go high (inactive).

6. **E clock frequency constraints**: (a) When performing a write followed by a read on successive E pulses at a high frequency, time must be allowed for status changes to occur. If E is a static part (no clock), successive write/read E pulses should be at least 500ns apart. (b) The E frequency should be high enough to move data through the FIFOs to service the peripheral requirements. The period between successive E pulses should be **less than** the period of RxC or TxC in order to maintain synchronisation between the data bus and the peripherals. This confirms the E clock must be faster than the serial clock.

7. **Clear-to-Send (CTS) real-time inhibit**: When CTS input is high, it provides a real-time inhibit to the TDRA status bit and its associated interrupt. All other status bits remain operational. Since CTS inhibits TDRA, CTS also inhibits the TDSR DMA request. The CTS input being high does not affect any other part of the transmitter — information in the Tx FIFO and Tx Shift Register will continue to be transmitted as long as the Tx CLK is running.

### Interrupt Handling

The ADLC generates NMI (Non-Maskable Interrupt) on the BBC Micro. The NMI is automatically enabled when the station ID register (&FE18) is read. This allows software to control when it's ready to handle network traffic.

## Econet Protocol

### Frame Format

Econet uses HDLC-like framing:

```
+------+----------+----------+----------+----------+---------+------+------+------+
| Flag | Dest Net | Dest Stn | Src Net  | Src Stn  | Control | Port | Data | CRC  |
+------+----------+----------+----------+----------+---------+------+------+------+
| 7E   | 1 byte   | 1 byte   | 1 byte   | 1 byte   | 1 byte  | 1 byte| var  | 2 bytes |
+------+----------+----------+----------+----------+---------+------+------+------+
```

- **Flag**: 0x7E marks frame boundaries
- **Destination/Source**: Network number (0 for local) + station number
- **Control byte**: Identifies frame type and sequence
- **Port**: Destination port number (determines protocol/service)
- **CRC**: 16-bit CRC-CCITT for error detection

### Four-Way Handshake

Econet uses a four-way handshake for reliable data transfer:

```
Sender                              Receiver
  |                                    |
  |------- Scout Frame --------------->|  (destination, port, control)
  |                                    |
  |<------ Scout Acknowledge ----------|  (confirms ready to receive)
  |                                    |
  |------- Data Frame(s) ------------->|  (actual payload)
  |                                    |
  |<------ Final Acknowledge ----------|  (confirms receipt)
  |                                    |
```

**Scout Frame**: Small frame announcing intent to transmit, containing destination address, port, and control byte.

**Scout Acknowledge**: Receiver confirms it exists and is ready.

**Data Frame(s)**: The actual data payload, potentially spanning multiple frames.

**Final Acknowledge**: Receiver confirms successful receipt.

### Standard Ports

| Port | Service |
|------|---------|
| &00 | Immediate operations |
| &90 | File server command |
| &91 | File server command reply |
| &92 | File server high-priority data |
| &93 | File server high-priority data reply |
| &94 | File server data |
| &95 | File server data reply |
| &99 | File server broadcast |
| &9C | Bridge protocol |
| &9D | Resource location |
| &D0 | SJ Research MDFS |
| &D1 | SJ Research Print Server |

### Immediate Operations

Port &00 is reserved for immediate operations - low-level commands that execute directly without NFS involvement:

| Code | Operation | Description |
|------|-----------|-------------|
| &81 | PEEK | Read memory from remote station |
| &82 | POKE | Write memory on remote station |
| &83 | JSR | Execute subroutine on remote station |
| &84 | User Procedure | Call OS routine on remote |
| &85 | OS Procedure | Reserved |
| &86 | Halt | Halt remote station |
| &87 | Continue | Resume halted station |
| &88 | Machine Type | Query machine type |

Stations can set a **protection mask** to restrict which immediate operations are allowed.

## AUN Protocol

AUN (Acorn Universal Networking) encapsulates Econet over UDP/IP. It was developed by Acorn for RISC OS machines but is now widely used for bridging.

### Key Differences from Native Econet

From the AUN Manager's Guide:
> "The transport protocol is User Datagram Protocol (UDP), enhanced by a proprietary handshake mechanism designed to support the semantics of Econet SWI calls. This is not a straightforward port of the four-way handshake mechanism used by native Econet, but is rather a **two-way handshake protocol** overlaid with a timeout and retransmission mechanism better suited to the characteristics of IP traffic."

AUN uses:
- **UDP** for transport (not TCP, which is stream-oriented and unsuited to Econet semantics)
- **IP** for network layer
- **ARP/RevARP** for address resolution
- **RIP** for routing information exchange

### Transport

- **Protocol**: UDP
- **Default Port**: 32768

### Packet Format

```
+------+------+------+------+--------+----------------+
| Type | Port | CB   | Pad  | Handle | Econet Payload |
+------+------+------+------+--------+----------------+
| 1    | 1    | 1    | 1    | 4      | variable       |
+------+------+------+------+--------+----------------+
```

- **Type**: Packet type (see below)
- **Port**: Econet port number
- **CB**: Control byte
- **Pad**: Padding byte (usually 0)
- **Handle**: 32-bit transaction handle for matching replies
- **Payload**: Econet data (without addressing, flags, CRC)

### AUN Packet Types

| Type | Name | Description |
|------|------|-------------|
| 1 | AUN_TYPE_BROADCAST | Broadcast packet |
| 2 | AUN_TYPE_UNICAST | Standard data packet |
| 3 | AUN_TYPE_ACK | Acknowledgement |
| 4 | AUN_TYPE_NACK | Negative acknowledgement |
| 5 | AUN_TYPE_IMMEDIATE | Immediate operation request |
| 6 | AUN_TYPE_IMM_REPLY | Immediate operation reply |

### AUN IP Address Format

AUN uses a Class A IP address format with netmask &FFFF0000:

```
1.network.net.station
```

| Field | Bytes | Description |
|-------|-------|-------------|
| site | 1 | Always 1 (reserved) |
| network | 1 | Logical network number (internal routing) |
| net | 1 | Econet net number |
| station | 1 | Econet station number |

**Examples:**
| Econet Address | AUN IP Address |
|----------------|----------------|
| 3.2 | 1.1.3.2 |
| 129.16 | 1.3.129.16 |

**Default (isolated network):** `1.0.128.station`

### Address Mapping (BeebEm Style)

BeebEm uses a simpler configuration file (`Econet.cfg`) with explicit mappings:
```
AUNMODE 1
AUNMAP 0.254 192.168.0.100
AUNMAP 0.253 192.168.0.101
```

This differs from Acorn's official AUN IP scheme and may be more practical for emulator-to-emulator and emulator-to-bridge communication.

## MOS Interface

### OSWORD Calls

**OSWORD &10 - Transmit**

Initiates a network transmission.

Control block at (XY):
```
Offset  Size  Description
0       1     Control byte
1       1     Port number
2       2     Destination station (network.station)
4       4     Buffer address
8       4     Buffer start offset
12      4     Buffer end offset
```

Returns status in control block byte 0:
- &00: Transmitted OK
- &40: Line jammed
- &41: Net error
- &42: Not listening
- &43: No clock
- &44: Transmit not started (bad control block)

**OSWORD &11 - Receive**

Opens a receive block to accept incoming data.

Control block at (XY):
```
Offset  Size  Description
0       1     Flag byte (0 to open, &7F to poll)
1       1     Port number (&00 = any)
2       2     Station number (&0000 = any)
4       4     Buffer address
8       4     Buffer start offset
12      4     Buffer end offset
```

Flag byte returns:
- &00: Receive block open, no data yet
- &FF: Data received successfully
- Other: Error codes

**OSWORD &12 - Read Arguments**

Reads information about a completed receive.

**OSWORD &13 - Read Station Info**

Returns local station number.

**OSWORD &14 - Read FS Info / Notify**

File server information and notification operations.

### OSBYTE Calls

| Call | Function |
|------|----------|
| &32 | Poll transmit |
| &33 | Poll receive |
| &34 | Delete receive block |
| &35 | Sever remote connection |
| &C9 | Read/write Econet OS call interception flag |
| &CE | Read/write Econet read character flag |
| &CF | Read/write Econet write character flag |
| &D0 | Read/write Econet OS RDCH/WRCH flag |

## BeebEm Implementation Analysis

BeebEm's Econet implementation (~2720 lines in `Econet.cpp`) provides the most comprehensive reference. The analysis below is based on the BeebEm Windows codebase (the most recently maintained version). The Mac port is structurally identical but has `#ifdef __APPLE__` guards for platform differences (socket types, memory alignment).

### Data Structures

**ADLC State:**
```cpp
struct MC6854 {
    unsigned char Control1, Control2, Control3, Control4;
    unsigned char TxFifo[3], RxFifo[3];
    unsigned char TxFifoPtr;       // Next free byte in TX FIFO (0-3)
    unsigned char RxFifoPtr;       // Next free byte in RX FIFO (0-3)
    unsigned char TxFifoTxLast;    // Bitmask: which FIFO slots are "last byte of frame"
    unsigned char RxFifoFCFlags;   // Bitmask: which RX slots are "frame complete"
    unsigned char RxFifoAPFlags;   // Bitmask: which RX slots are "address present"
    unsigned char Status1, Status2;
    int PriorityStatus;            // PSE level (0=inactive, 1-4=priority tiers)
    bool CTS, Idle;
};
```

**Four-Way Handshake State Machine (10 states):**
```cpp
enum class FourWayStage {
    Idle,                // No transaction
    ScoutSent,           // TX: scout queued, waiting for timeout to fake ack
    ScoutAckReceived,    // TX: fake ack given to Beeb, awaiting data response
    DataSent,            // TX: data sent to network, waiting for remote ACK
    WaitForIdle,         // Transaction complete, buffers draining
    ScoutReceived,       // RX: scout from network, awaiting Beeb's ack
    ScoutAckSent,        // RX: ack sent, waiting for timeout to deliver data
    DataReceived,        // RX: data delivered to Beeb, awaiting Beeb's final ack
    ImmediateSent,       // Immediate op sent, waiting for reply
    ImmediateReceived    // Immediate reply received, awaiting Beeb's response
};
```

**AUN Header (8 bytes on wire):**
```cpp
struct AUNHeaderType {
    AUNType Type;           // 1=Broadcast, 2=Unicast, 3=Ack, 4=NAck, 5=Immediate, 6=ImmReply
    unsigned char Port;     // Econet port number
    unsigned char CtrlByte; // Control byte (bit 7 cleared for AUN)
    unsigned char Pad;      // Retransmission count (usually 0)
    uint32_t Handle;        // 4-byte sequence number (little-endian)
};
```

**Packet Buffers:**
```
Non-AUN: Beeb -> ADLC.TxFifo -> BeebTx -> sendto()
         recvfrom() -> BeebRx -> ADLC.RxFifo -> Beeb

AUN:     Beeb -> ADLC.TxFifo -> BeebTx -> EconetTx -> sendto()
         recvfrom() -> EconetRx -> BeebRx -> ADLC.RxFifo -> Beeb
```

### ADLC Register Access

**CPU Write (`EconetWrite`):**
- Register 0: Always CR1
- Register 1 with CR1b0=0: CR2
- Register 1 with CR1b0=1: CR3
- Register 3 with CR1b0=1: CR4
- Register 2 or (Register 3 with CR1b0=0): TX data
  - Data shifts through FIFO: `TxFifo[2]=TxFifo[1]; TxFifo[1]=TxFifo[0]; TxFifo[0]=Value`
  - `TxFifoPtr++; TxFifoTxLast<<=1`
  - Register 3 automatically sets CR2b4 (TX_LAST_DATA)
  - All blocked if TX reset (CR1b7) is set

**CPU Read (`EconetRead`):**
- Register 0: SR1 (no side effects)
- Register 1: SR2 (no side effects)
- Register 2 or 3: RX FIFO pop: `Value = RxFifo[--RxFifoPtr]`
  - Sets `EconetStateChanged` to trigger immediate poll
  - Returns 0 if RX reset or FIFO empty

### Polling Architecture

`EconetPoll()` is called from the main CPU loop after every instruction. It's a gate:

```cpp
bool EconetPoll() {
    if (EconetStateChanged || EconetTrigger <= TotalCycles) {
        EconetStateChanged = false;
        if (Socket != INVALID_SOCKET) return EconetPollReal();
    }
    return false;
}
```

`EconetPollReal()` (~680 lines) does three things on each call:
1. **Process control register actions** (auto-clearing bits, resets)
2. **Trickle data** between FIFOs and packet buffers (only when timer fires)
3. **Recompute all status bits** and determine if interrupt needed

Returns `true` to request NMI.

### Control Register Processing (in `EconetPollReal`)

Certain control bits are "write-once, auto-clear" and are processed each poll:

| Bit | Auto-clear action |
|-----|-------------------|
| CR1b5 (RX Frame Discontinue) | Clears RX buffers, resets to Idle, then clears itself |
| CR2b4 (TX Last Data) | Sets `TxFifoTxLast |= 1`, then clears itself |
| CR2b5 (Clear RX Status) | Clears SR1b1,b3 and SR2b1,b2,b3,b4,b5,b6; advances PSE; then clears itself |
| CR2b6 (Clear TX Status) | Clears SR1b4,b5,b6; re-sets CTS if line still high; then clears itself |
| CR4b5 (TX Abort) | Clears TX FIFO and buffers, resets to Idle, then clears itself |

On CR1b6 (RX Reset): additionally clears all RX buffers and FIFO.
On CR1b7 (TX Reset): additionally clears all TX buffers and FIFO.

### TX Data Flow

Every `TimeBetweenBytes` cycles (default 128, ~64us), one byte transfers from FIFO to `BeebTx`:

```
1. Pull byte: BeebTx.Buffer[Pointer] = TxFifo[--TxFifoPtr]
2. If TxFifoTxLast bit set for that slot: this is last byte of frame
3. On last byte: call EconetSendPacket() to transmit entire assembled frame
```

### RX Data Flow

Every `TimeBetweenBytes` cycles, one byte transfers from `BeebRx` into FIFO:

```
1. Shift FIFO: RxFifo[2]=RxFifo[1]; RxFifo[1]=RxFifo[0]; RxFifo[0]=BeebRx.Buffer[Pointer]
2. RxFifoPtr++; shift FC and AP flags left
3. First byte (Pointer==0): set AP flag
4. Last byte (Pointer >= BytesInBuffer): set FC flag, reset buffer
```

When FIFO is empty AND no Frame Valid flag pending, `EconetReceivePacket()` attempts non-blocking UDP read via `select()` with zero timeout.

### Status Register Derivation

Status bits are fully recomputed every poll cycle:

**SR1:**
| Bit | Derivation |
|-----|------------|
| b0 RDA | `RxFifoPtr > 0` (1-byte mode) or `> 1` (2-byte mode); mirrored to SR2b7 |
| b1 S2RQ | Set when new bits appear in SR2 (excluding RDA); cleared when cause bits clear |
| b2 Loop | Always 0 (unsupported) |
| b3 FD | Follows `FlagFillActive` |
| b4 CTS | Set when `!Socket || !(CR2b7 RTS)`; latched until CPU clears |
| b5 TxU | Set on FIFO overflow (TxFifoPtr > 4) |
| b6 TDRA/FC | TDRA mode: space in FIFO AND CTS low AND DCD low. FC mode: TxFifoPtr == 0 |
| b7 IRQ | Set when interrupt causes detected; cleared when all causes resolved |

**SR2:**
| Bit | Derivation |
|-----|------------|
| b0 AP | `RxFifoAPFlags` bit set for current top-of-FIFO slot |
| b1 FV | `RxFifoFCFlags` bit set; only cleared by Clear RX Status or RX Reset |
| b2 Idle | `Idle && !FlagFillActive` |
| b3 RxAbort | Not used (always 0 - no abort simulation) |
| b4 FCS Error | Not used (always 0 - UDP has own checksums) |
| b5 DCD | `Socket == INVALID_SOCKET` (no clock = no socket) |
| b6 Overrun | Set on FIFO overflow (RxFifoPtr > 4) |
| b7 RDA | Copy of SR1b0 |

### Prioritised Status Enable (PSE)

When CR2b0 is set, SR2 RX bits are filtered to show only the highest-priority condition:

```
Priority 1 (highest): FV, Abort, FCS Error, DCD, Overrun → suppresses AP, Idle, RDA
Priority 2: Idle → suppresses AP, RDA
Priority 3: AP → suppresses RDA
Priority 4 (lowest): RDA → suppresses FV
```

Each Clear RX Status advances to the next priority level. The BBC typically doesn't use PSE (the NFS ROM handles all bits directly).

### Interrupt Generation

Edge-triggered: interrupts fire on 0→1 transitions of status bits:

```
1. Save previous SR1, SR2
2. Recompute all status bits
3. For SR2: new_bits = (SR2 ^ PrevSR2) & SR2 & ~RDA
   - If RIE enabled and new_bits: set S2RQ, accumulate cause
4. For SR1: new_bits = (SR1 ^ PrevSR1) & SR1 & ~IRQ
   - Mask by RIE (for RDA, S2RQ, FD) and TIE (for CTS, TxU, TDRA)
   - If new_bits: set IRQ, return true (trigger NMI)
5. For cleared bits: remove from cause; if all causes gone, clear IRQ
```

### NMI Enable/Disable (INTON/INTOFF)

The ADLC NMI is gated by a flip-flop (IC97) controlled by address bus decoding:

- **INTOFF** (NMI disabled): Reading &FE18-&FE1F (station ID register)
  - Same read also returns the station number
  - On Model B: `(Address & ~3) == 0xFE18`
  - On Master: `(Address & ~3) == 0xFE38`
- **INTON** (NMI enabled): Reading &FE20-&FE27 (Video ULA range)
  - On Model B: `(Address & ~3) == 0xFE20`
  - On Master: `(Address & ~3) == 0xFE3C`

**Critical detail:** When INTON fires and there's a pending interrupt (IRQ flag already set in SR1), NMI is asserted immediately. This allows software to read the station number with interrupts disabled, configure the ADLC, then enable NMI when ready.

### AUN Four-Way Handshake Simulation

AUN uses a two-way handshake (data + ack) but the Beeb's NFS ROM expects a four-way (scout + scout-ack + data + final-ack). BeebEm bridges this gap by *faking* the scout phase locally:

**Sending a unicast:**
```
1. Beeb writes scout → EconetSendPacket() with state=Idle
   - Does NOT send anything on network
   - State → ScoutSent, arms timeout (5000 cycles / ~2.5ms)
   - Saves scout header in EconetTx for later

2. Timeout fires → EconetReceivePacket() fakes scout ack
   - Generates 4-byte ack in BeebRx (src = original destination)
   - State → ScoutAckReceived

3. Beeb writes data frame → EconetSendPacket() with state=ScoutAckReceived
   - NOW sends AUN Unicast packet via UDP (header + payload)
   - State → DataSent

4. Remote sends AUN Ack → EconetReceivePacket()
   - Generates 4-byte final ack in BeebRx
   - State → WaitForIdle → Idle (when buffers drain)
```

**Receiving a unicast:**
```
1. AUN Unicast packet arrives → EconetReceivePacket() with state=Idle
   - Constructs scout (header only, or header + 4/8 bytes) in BeebRx
   - State → ScoutReceived
   - Stores full AUN packet in EconetRx for later

2. Beeb sends scout ack → EconetSendPacket() with state=ScoutReceived
   - Does NOT send anything on network
   - State → ScoutAckSent, arms timeout

3. Timeout fires → EconetReceivePacket() delivers cached data
   - Copies remaining payload from EconetRx into BeebRx
   - State → DataReceived

4. Beeb sends final ack → EconetSendPacket() with state=DataReceived
   - Sends AUN Ack packet to remote
   - State → WaitForIdle → Idle
```

**Broadcasts:** Single packet, no handshake. State: Idle → WaitForIdle → Idle.

**Immediate ops:** Single exchange (command + reply). State: Idle → ImmediateSent → WaitForIdle → Idle.

### Scout Payload Sizes

The scout/data split depends on the control byte (with bit 7 masked):

| Control Byte | Scout carries | Data carries |
|-------------|---------------|--------------|
| 0x02 (0x82 & 0x7f) | 8 bytes | remaining from offset 8 |
| 0x03-0x05 (0x83-0x85 & 0x7f) | 4 bytes | remaining from offset 4 |
| Other | 0 bytes (header only) | all payload |

### Pseudo Flag Fill

Flag fill prevents collisions on real Econet. BeebEm approximates it:

- **Set** when: we send a packet (peer assumed busy), or see traffic for another station
- **Cleared** when: we receive a packet addressed to us, or timeout expires, or handshake completes
- **Timeout**: 500,000 cycles (~250ms), configurable via `FLAGFILLTIMEOUT`
- **Effect**: SR1b3 (Flag Detected) follows `FlagFillActive`

### Idle Detection

`ADLC.Idle = true` when all of:
- RX not reset
- FIFO empty (`RxFifoPtr == 0`)
- No Frame Valid flag set
- No incoming data waiting (`BeebRx.BytesInBuffer == 0`)

### Timeouts

| Timer | Default | Purpose |
|-------|---------|---------|
| TimeBetweenBytes | 128 cycles (~64us) | Byte trickle rate between FIFO and buffers |
| EconetScoutAckTimeout | 5,000 cycles (~2.5ms) | Delay before faking scout ack |
| FourWayStageTimeout | 500,000 cycles (~250ms) | Watchdog: force-reset hung transactions |
| EconetFlagFillTimeout | 500,000 cycles (~250ms) | Assume peer finished processing |

### Timing Rationale

Econet clock: up to 250kHz. At 250kHz, one byte (8 bits + bit-stuffing overhead) takes ~40us, or ~80 CPU cycles at 2MHz. BeebEm uses 128 cycles (~64us) as a compromise - slightly slower than hardware but gives software time to keep up. The comment notes that 64 cycles was "a bit fast for netmon prog to keep up".

### Operating Modes

1. **AUNMODE 0**: Raw Econet frames over UDP. No four-way handshake simulation. Direct packet passthrough.
2. **AUNMODE 1**: AUN protocol. Four-way handshake faked locally. AUN header added/stripped.

### Simplifications vs Real Hardware

| Aspect | Real ADLC | BeebEm |
|--------|-----------|--------|
| Collision detection | CTS reflects bus contention | CTS = !(Socket && RTS) |
| CRC/FCS | 16-bit CRC-CCITT generated and checked | Not simulated (UDP checksums suffice) |
| Flag bytes (0x7E) | Transmitted on wire between frames | Not simulated (UDP packet boundaries) |
| Bit stuffing | Zero-insertion after 5 consecutive 1s | Not simulated |
| Clock detection | DCD reflects physical clock lock | DCD = !(Socket open) |
| Abort on wire | 7+ consecutive 1s | TX Abort clears FIFO but doesn't signal remote |
| Frame sequence numbers | HDLC sequence numbering | Single 32-bit handle incremented by 4 |
| Multi-station addressing | Multiple address modes | Fixed network.station |
| RTS/DTR signals | Drive physical pins | Logical only (affect CTS calculation) |
| Flag fill | Hardware monitors bus | Pseudo algorithm with timeouts |

### Configuration

**Econet.cfg format:**
```
# Comments start with #
AUNMODE 1
LEARN 0
AUNSTRICT 0
MASSAGENETS 0
FLAGFILLTIMEOUT 500000
SCOUTACKTIMEOUT 5000
TIMEBETWEENBYTES 128
FOURWAYTIMEOUT 500000

# Station definitions: network station ip port
0 254 127.0.0.1 32768
0 1 127.0.0.1 32769
```

**AUNMap file** (optional, only used when AUNMODE=1):
```
AddMap 192.168.0.0 128    # Subnet → Econet network number mapping
```

MASSAGENETS translates between Econet network numbers (0-127) and AUN network numbers (128-255) by toggling bit 7.

## Cycle-Accurate ADLC Design

Beebium takes a more principled approach than BeebEm's poll-based model, emulating the MC68B54 with cycle-accurate timing on the E-clock domain while abstracting the serial bit-level domain.

### Two Clock Domains

The real ADLC has two independent clock domains:

1. **E-clock domain** (system bus): Register access, FIFO advancement, status register derivation, interrupt generation. This is what the CPU interacts with.

2. **TxC/RxC domain** (serial bit clock): Bit-level framing — zero insertion/deletion, flag detection, CRC generation/checking. The Econet clock box provides ~200 kHz for ~200 kbit/s.

Since Beebium uses AUN-over-UDP rather than a real Econet wire, only the E-clock domain needs cycle-accurate emulation. The serial domain is abstracted to byte/frame-level timed events. The CPU sees perfectly accurate register behaviour, but bit-level operations (zero insertion, CRC, flag bytes) that serve no purpose over UDP are not modelled.

### ADLC E Clock: Confirmed 2MHz

The MC68B54 ADLC on the BBC Micro has its E pin connected to the **2MHz system clock**, not 1MHzE. Evidence:

1. **Bus stretching mask**: $FEA0-$BEBF is marked "fast" (no stretching) in `BusStretching.hpp`, confirmed by jsbeeb and beebjit
2. **BeebEm**: No `SyncIO()` applied to Econet register accesses (unlike VIAs, CRTC, ACIA which all apply 1MHz synchronisation)
3. **BeebEm comment** (`Econet.cpp:171`): "max 250Khz network clock. **2MHz system clock**. one click every 8 cycles."
4. **MC68B54 datasheet**: The "B" variant is rated for 2.0 MHz maximum E frequency (500ns minimum cycle time)
5. **MC68B54 datasheet**: "E should be a free-running clock such as the MC6800 MPU system clock"

The ADLC is analogous to the Tube ULA: fast, no stretching, 2MHz. This is not a 1MHz bus device despite its physical proximity to the 1MHz bus area on the PCB.

### FIFO Timing Model

On real hardware, data moves through the 3-byte FIFOs on **both phases** of the E clock (rising and falling). Beebium models this:

- **tick_rising()**: Advance TX FIFO towards the serial output; advance RX FIFO from serial input towards CPU. Update FIFO pointers and frame boundary markers.
- **tick_falling()**: Complete the transfer. Update status bits based on new FIFO state.

The simulated "serial clock" determines when new bytes enter the RX FIFO or leave the TX FIFO. Rather than modelling individual bits at 200 kHz, the network backend delivers/consumes whole bytes at a configurable rate (default: one byte every 128 half-cycles of the 2MHz E-clock, i.e. 64 full cycles = ~32 us). Both rising and falling edges increment the byte timer, matching BeebEm's `TimeBetweenBytes` of 128 which counts in poll cycles (effectively CPU cycles).

**FIFO entry encoding** (adopted from MAME's approach): Each FIFO position is a `uint16_t` where the lower 8 bits carry the data byte and upper bits carry co-located metadata:

| Bit | Mask | Meaning |
|-----|------|---------|
| 0-7 | 0x00FF | Data byte |
| 8 | 0x0100 | Entry valid (slot occupied) |
| 9 | 0x0200 | Last byte of frame |
| 10 | 0x0400 | Address Present (RX only) |

This keeps metadata co-located with its data, so FIFO shifts are a single move per slot rather than parallel shifts of separate data and bitmask arrays. The valid bit (0x100) eliminates the need for a separate FIFO pointer — the number of occupied slots is implicit in which entries have bit 8 set.

**Frame boundary tracking** uses the datasheet's pointer model:
- **TX**: Writing to "Frame Continue" ($FEA2) sets the frame boundary pointer (bit 9 clear). Writing to "Frame Terminate" ($FEA3) sets bit 9 on the entry. When a negative transition is detected at the third FIFO location (entry with bit 9 set reaches the output), the transmitter appends FCS and closing flag.
- **RX**: Address Present (bit 10) marks address bytes of a frame. Frame Valid (FV) is set when the last byte (bit 9 set) reaches the third FIFO location. Once FV is set, further data transfer to the last location is blocked until status is cleared.

### Frame Field State Machine

Both TX and RX paths track which HDLC frame field is being processed, using a state machine adopted from MAME's approach:

| State | Field | Length | Transition |
|-------|-------|--------|------------|
| 0 | Idle | — | First FIFO push starts frame → state 2 |
| 1 | Flag sync (RX only) | — | Flag detected → state 2 |
| 2 | Address | 8 bits | If AEX and bit 0=0: stay in 2 (more address bytes); else → 3 |
| 3 | Control | 8 bits | If CEX: → 4; else if LCF: → 5; else → 6 |
| 4 | Extended control | 8 bits | If LCF: → 5; else → 6 |
| 5 | Logical control | 8 bits | If bit 7=1: stay in 5 (more LCF bytes); else → 6 |
| 6 | Data | TWL/RWL bits | Until frame terminates → 0 |

This state machine is needed to:
- **Correctly set AP**: Only on address bytes (state 2), not on control or data
- **Handle extended addressing (AEX)**: Econet uses 4-byte addressing (dest net, dest stn, src net, src stn) — multiple address bytes with bit 0 continuation
- **Determine word length**: Address/control fields are always 8 bits; data fields use CR4's TWL (TX) or RWL (RX) setting (5/6/7/8 bits, though Econet always uses 8)

### Status Register Model

Status bits are updated synchronously on E-clock edges, not lazily at poll time:

- **Present conditions** (continuously derived from current state): RDA, TDRA, Loop, Idle
- **Stored conditions** (latched on transitions, cleared by CPU): FD, CTS, TxU, AP, FV, ERR, OVRN
- **Dual-nature conditions** (logical OR of stored latch + present input): DCD, RxABT, Rx Idle
  - DCD: stored positive-edge latch OR current DCD input level
  - RxABT: stored in-frame abort latch OR Rx abort detect logic
  - Rx Idle: stored inactive idle latch OR receiver idling detector
  - Clearing the stored latch reveals the present condition — if the input is still asserted, the status bit remains set

On each E-clock falling edge:
1. Derive present conditions from FIFO state and input pins
2. Detect transitions for stored conditions (0→1 edges); latch new stored conditions
3. Combine stored + present for dual-nature bits
4. Apply PSE priority filtering if enabled
5. Compute SR1/SR2 from combined state
6. Derive IRQ output: `IRQ = (RIE && rx_cause) || (TIE && tx_cause)`

**Clearing status in prioritised mode**: A status condition must be read before it can be cleared. Clearing a higher-priority condition may unmask a lower-priority one, resulting in a new IRQ. This prevents inadvertent loss of status conditions.

The IRQ output asserts on the **same E-cycle** that sets the triggering status bit — matching the datasheet's specification that interrupt timing is synchronous to E.

### NMI Integration

The ADLC's IRQ output (active low) connects to the BBC Micro's NMI line, gated by the INTON/INTOFF hardware:

**Beebium integration:**
- `EconetSocket` (not `Mc6854` directly) satisfies the `NmiSource` role: `bool nmi_pending() const` returns `enabled_ && nmi_enable_ff_ && adlc_.irq_output()`
- The NMI gating flip-flop (`nmi_enable_ff_`) lives in `EconetSocket`, not in `Mc6854` — matching the physical hardware where IC97 is separate from IC93
- New mask constant: `kEconetNmiDeviceMask = 0x02` (bit 1, alongside disc controller's bit 0)
- Hardware's NMI aggregation includes both disc and Econet sources
- The Econet NMI is **not** subject to the 1MHz sampling restriction applied to the disc controller (since the ADLC runs at 2MHz and its IRQ output updates at 2MHz, it can be polled every cycle)

**NMI gating** is glue logic within `EconetSocket`, external to the ADLC:
- INTOFF: side-effect of reading station ID register ($FE18) — clears `nmi_enable_ff_`
- INTON: side-effect of accessing Video ULA range ($FE20) — sets `nmi_enable_ff_`
- When INTON fires with a pending ADLC IRQ, NMI asserts on the next poll cycle
- When socket is empty (no Econet fitted): `nmi_pending()` always returns false

### Machine::step() Integration

The `EconetSocket` integrates into `Machine::step()` as a 2MHz peripheral. The socket delegates to `Mc6854` when enabled, and is a no-op when empty:

```
step() {
    // ... existing CPU tick, video tick, VIA tick ...

    // Tick Econet (delegates to ADLC when hardware is fitted; no-op when empty)
    if (is_rising) memory.econet_socket.tick_rising();
    else memory.econet_socket.tick_falling();

    // ... existing IRQ poll ...

    // NMI poll: aggregate disc + Econet
    // Disc NMI polled at 1MHz; Econet NMI polled at 2MHz (ADLC is a fast device)
    uint8_t nmi_mask = 0;
    if ((cycle_count & 1) == 0) {
        nmi_mask |= memory.poll_disc_nmi();   // Disc at 1MHz
    }
    nmi_mask |= memory.econet_socket.nmi_pending() ? kEconetNmiDeviceMask : 0;
    M6502_SetDeviceNMI(&cpu, kDiscNmiDeviceMask | kEconetNmiDeviceMask, nmi_mask);
}
```

Note: No `if (econet_enabled)` guard needed — `EconetSocket::tick_rising()`, `tick_falling()`, and `nmi_pending()` are all no-ops when the socket is empty, matching the `DiscControllerSocket` pattern where the socket handles the null case internally.

### Class Structure (as implemented)

```
EconetSocket (member of MemoryMap — analogous to DiscControllerSocket)
├── backend_                         // unique_ptr<NetworkBackend> — owns the backend chain
├── handshake_                       // unique_ptr<FourWayHandshake> — AUN protocol bridge (optional)
├── adlc_                            // unique_ptr<Mc6854> — ADLC instance
├── station_id_                      // Station number (from --station N)
├── enabled_                         // Whether Econet hardware is "fitted"
├── nmi_enable_ff_                   // INTON/INTOFF flip-flop (IC97 glue logic)
├── last_bus_value_ptr_              // Pointer to MemoryMap's last_bus_value for open bus
├── enable(station_id, backend, aun_mode)  // Fit Econet hardware
│   └── When aun_mode: backend_ → FourWayHandshake → Mc6854
│   └── When !aun_mode: backend_ → Mc6854 directly
├── disable()                        // Remove Econet hardware
├── Station ID Region (mapped to &FE18-&FE1F)
│   ├── read_station_id() -> uint8_t // When enabled: station_id_ + INTOFF. When empty: 0x00
│   └── write_station_id()           // Ignored (read-only register) + INTOFF
├── ADLC Region (mapped to &FEA0-&FEBF)
│   ├── read_adlc(offset) -> uint8_t // When enabled: adlc_->read(). When empty: last bus value
│   └── write_adlc(offset, value)    // When enabled: adlc_->write(). When empty: ignored
├── on_inton()                       // Called from Video ULA hook; sets nmi_enable_ff_
├── nmi_pending() -> bool            // enabled_ && nmi_enable_ff_ && adlc_->irq_output()
├── tick_rising()                    // Ticks handshake_ then adlc_ when enabled
├── tick_falling()                   // Ticks adlc_ when enabled
└── reset()                          // Hard-resets adlc_ and handshake_

Mc6854 (pure ADLC hardware — no knowledge of BBC-specific glue logic)
├── Registers
│   ├── cr1_, cr2_, cr3_, cr4_       // Control register latches
│   └── sr1_, sr2_                   // Derived status (updated synchronously)
├── TX Path
│   ├── tx_fifo_[3]                  // uint16_t: data (0-7) + valid/last/AP metadata (8-10)
│   ├── tx_frame_field_              // FrameField enum: Idle/Flag/Address/Control/ExtCtrl/Lcf/Data
│   └── tx_frame_buffer_             // Assembled frame awaiting network send
├── RX Path
│   ├── rx_fifo_[3]                  // uint16_t: data (0-7) + valid/last/AP metadata (8-10)
│   ├── rx_frame_field_              // FrameField enum (same as TX)
│   ├── rx_frame_buffer_             // Received frame being trickled into FIFO
│   └── rx_buffer_index_             // Position in rx_frame_buffer_
├── Status Latches
│   ├── fd_stored_, cts_stored_, txu_stored_        // SR1 stored conditions
│   ├── fv_stored_, fv_deferred_, frame_boundary_   // FV timing state
│   ├── err_stored_, abt_stored_, ovrn_stored_      // SR2 stored conditions
│   ├── idle_stored_, dcd_stored_                    // SR2 dual-nature stored
│   └── prev_dcd_input_, prev_cts_input_            // Edge detection state
├── Timing
│   ├── byte_timer_                  // Countdown for next byte transfer
│   └── byte_period_                 // Configurable (default 128 half-cycles = 32us)
├── Interface
│   ├── read(offset) -> uint8_t      // CPU register read (offsets 0-3)
│   ├── write(offset, value)         // CPU register write (offsets 0-3)
│   ├── tick_rising()                // E-clock rising edge (advance byte timer, update status)
│   ├── tick_falling()               // E-clock falling edge (advance byte timer, update status)
│   └── irq_output() -> bool         // ADLC IRQ pin state
└── NetworkBackend& backend_         // Injected reference (not owned)

FourWayHandshake (decorator: sits between Mc6854 and AunBackend)
├── Implements NetworkBackend         // ADLC talks to this as if it were the real backend
├── stage_                           // 10-state FSM (Idle, ScoutSent, ScoutAckReceived, ...)
├── Timers
│   ├── handshake_timer_             // Timeout for current handshake phase
│   ├── watchdog_timer_              // Force-reset hung transactions (~250ms)
│   ├── flag_fill_timer_             // Pseudo flag fill timeout (~250ms)
│   └── idle_cooldown_               // Inter-handshake gap (~2.5ms)
├── is_receiving_flags()             // Drives ADLC SR1 FD (flag fill simulation)
├── is_expecting_frame()             // Suppresses INACTIVE during inter-frame gaps
├── tick()                           // Called once per 2MHz rising edge, before ADLC tick
└── NetworkBackend& backend_         // The real backend (AunBackend)

AunBackend (UDP transport — implements NetworkBackend)
├── send_frame(NetworkFrame)         // Encodes and sends AUN packet via UDP sendto()
├── receive_frame() -> optional      // Non-blocking receive via select() + recvfrom()
├── is_connected() -> bool           // Socket bound and valid
├── add_peer(net, stn, ip, port)     // Explicit peer mapping
├── remove_peer(net, stn)            // Remove peer mapping
└── Peer Table
    ├── forward_map_                 // (net,stn) → (ip,port)
    └── reverse_map_                 // (ip,port) → (net,stn)

NetworkBackend (abstract interface)
├── send_frame(const NetworkFrame&)  // Send a typed frame
├── receive_frame() -> optional      // Non-blocking receive
├── is_connected() -> bool           // Carrier/clock status for DCD
├── is_receiving_flags() -> bool     // Flag fill for SR1 FD (default: false)
└── is_expecting_frame() -> bool     // Suppress INACTIVE in handshake gaps (default: false)

TestBackend (test double — implements NetworkBackend)
├── inject_rx_frame() / inject_rx_network_frame()  // Queue frames for ADLC to receive
├── sent_frames() / sent_network_frames()          // Inspect what the ADLC transmitted
└── set_connected() / set_flags_active()           // Control DCD and flag fill
```

**Backend chain in AUN mode**: `Mc6854 → FourWayHandshake → AunBackend → UDP socket`. The FourWayHandshake is a decorator that implements `NetworkBackend` and wraps the real `AunBackend`. The ADLC sees raw Econet frames (scout, scout-ack, data, final-ack); the FourWayHandshake translates to/from typed AUN packets (Unicast, Ack, Broadcast, Immediate). Scout phases are generated locally with timeouts; only data and ack packets cross the real network.

**Separation of concerns**: The `Mc6854` is a pure ADLC emulation with no knowledge of BBC-specific details (NMI gating, station ID register, address decoding) or AUN protocol details. The `EconetSocket` provides the BBC-specific glue logic. The `FourWayHandshake` provides the AUN↔Econet protocol bridge. This mirrors the physical hardware: IC93 (ADLC) is a Motorola part; IC97 (flip-flop) and the address decoding are Acorn's glue logic; the network protocol is handled by software and the clock box.

The network backend is injected into `Mc6854`, keeping it as pure hardware emulation. `TestBackend` replaces the real UDP backend for deterministic testing.

### Comparison with BeebEm and MAME

| Aspect | BeebEm | MAME | Beebium |
|--------|--------|------|---------|
| Tick granularity | Once per CPU instruction | Timer callbacks | Every 2MHz half-cycle (rising + falling) |
| FIFO model | `uint8_t[3]` + separate bitmask arrays | `uint16_t[3]` with metadata in upper bits | `uint16_t[3]` with metadata in upper bits (adopted from MAME) |
| Frame field tracking | None (implicit in packet state) | 7-state machine (idle/flag/addr/ctrl/ext-ctrl/lcf/data) | 7-state machine (adopted from MAME) |
| Status derivation | Recomputed at poll time | Level-sensitive recomputation | Updated synchronously on E-clock falling edges |
| Stored vs present status | Not distinguished | Not distinguished | Explicit model for DCD, RxABT, Rx Idle |
| NMI timing | Edge detection at poll time (may be late) | N/A (drives IRQ line directly) | Asserts on same E-cycle as triggering status bit |
| E-clock phases | Not modelled | Not modelled | Both phases advance FIFO state |
| PSE (priority filtering) | Partial (4-tier) | Not implemented (TODO) | Full per datasheet |
| Extended addressing (AEX) | Not modelled | Bit 0 continuation check | Bit 0 continuation check (adopted from MAME) |
| CRC | Not simulated | Hardcoded placeholder (TODO) | Not simulated (AUN/UDP provides checksums) |
| Word length (CR4) | Not modelled | Full 5/6/7/8 bit support | Full 5/6/7/8 bit support |
| Serial bit-level | Not modelled | Full (zero insertion/deletion, flag/abort detect) | Not modelled (abstracted to byte/frame level) |
| Frame-level interface | Implicit (packet buffers) | Explicit (`send_frame()` / `out_frame_cb()`) | Explicit (injected network backend) |
| Testability | Integrated with BeebEm global state | MAME device framework | Isolated class with injected network backend |

## Pi Econet Bridge

The Pi Econet Bridge is a Raspberry Pi-based gateway between real Econet networks and AUN/IP networks.

### Hardware

- Raspberry Pi with custom Econet interface board
- Directly connects to Econet cable
- Kernel module handles timing-critical Econet operations

### Network Architecture

```
BBC Micro <--Econet--> Pi Bridge <--AUN/UDP--> Beebium
                           |
                           +--AUN/UDP--> RISC OS
                           |
                           +--AUN/UDP--> Other bridges
```

### Configuration

The Pi Econet Bridge uses configuration for:
- Local station number
- Network number mappings
- IP address to station mappings
- File server locations

### Compatibility Notes

For Beebium to work with Pi Econet Bridge:
1. Must implement AUN protocol correctly
2. Must handle bridge-specific packet types
3. May need to handle network number translation
4. Should support dynamic station discovery

## NFS/ANFS ROMs

To use Econet file servers, BBC Micros need appropriate network filing system ROMs.

### ROM Versions

| ROM | Machine | Notes |
|-----|---------|-------|
| NFS 3.34 | Model B / B+ | Original network filing system (`roms/acorn-nfs_3_34.rom`, 8K) |
| NFS 3.60 | Model B / B+ | Later version, combined with DFS in DNFS ROM |
| ANFS 4.18 | Master 128 | Advanced NFS for Master series |
| ANFS 4.25 | Master 128 | Later ANFS version |

### DNFS ROM

The DNFS (Disc and Network Filing System) ROM combines DFS 1.20 and NFS 3.60 into a single 16K ROM. From the DNFS manual:

- **Auto-detection:** DNFS checks for Econet and/or disc hardware at startup
- **Priority:** If both are present, DFS takes priority by default
- **Selection:** Keyboard switch 1 (link S1) overrides to select NFS at boot
- **Commands:** Use `*NET` or `*DISC` to switch filing systems at runtime

**Important limitation:** DNFS's DFS 1.20 component requires the Intel 8271 FDC. There is no DNFS ROM with a WD1770-compatible DFS. Since Beebium currently only supports the WD1770 FDC, we cannot use DNFS. Instead, we use separate ROMs in two sideways slots:
- DFS 2.x (WD1770-compatible) — e.g. `roms/acorn-dfs_2_26.rom`
- NFS 3.34 (standalone) — `roms/acorn-nfs_3_34.rom`

This is the default configuration for Econet-capable Beebium machines with disc support.

**NFS 3.60 improvements over 3.34:**
- Multi-column catalogue display
- Password masking at logon
- Control characters allowed in printer protocols (for graphics dumps)
- Econet runs as IRQ task (foreground activities don't block network)
- No "privileged" station numbers - all stations protected against immediate operations

### ROM Interaction

NFS/ANFS ROMs:
- Hook into MOS vectors for filing system operations
- Use OSWORD &10-&14 for network operations
- Provide *NET, *I AM, *BYE, *SDISC commands
- Implement file server protocol over Econet

Note: NFS can coexist with DFS/ADFS - users select filing system with *DISC, *NET, etc.

## Implementation Status

The core Econet/AUN implementation is complete. This section summarises what was planned, what was built, and what differs from the original design. For remaining integration work (presets, gRPC, service discovery, clients), see `docs/econet-integration.md`.

### Completed: ADLC Hardware Emulation (Mc6854.hpp)

1. **Create `Mc6854` class** as a `ClockSubscriber` and `NmiSource`
   - `clock_rate = Rate_2MHz`, `clock_edges = Both` — ticked every 2MHz half-cycle
   - Four control registers (CR1-CR4) with correct addressing:
     - Register 0 write: always CR1
     - Register 1 write: CR2 (when CR1b0=0) or CR3 (when CR1b0=1)
     - Register 3 write with CR1b0=1: CR4
     - Register 2/3 write with CR1b0=0: TX data (register 3 auto-sets TX_LAST)
   - Status registers (SR1, SR2) updated synchronously on E-clock falling edges
   - 3-byte transmit and receive FIFOs advanced on both E phases (rising + falling)
   - FIFO entries are `uint16_t`: data in bits 0-7, metadata in bits 8-10 (valid, last-byte, address-present)
   - Frame field state machine: tracks address/control/data phases for correct AP handling and word length
   - Extended addressing (AEX): bit 0 continuation check for multi-byte address fields
   - Word length (CR4): full 5/6/7/8 bit support for TX (TWL) and RX (RWL)
   - PSE (Prioritised Status Enable) with full 4-tier priority filtering per datasheet
   - Auto-clearing control bits: CR1b5, CR2b4, CR2b5, CR2b6, CR4b5
   - Edge-triggered interrupt: IRQ asserts on same E-cycle as triggering status bit
   - Injected network backend (for testability — test doubles replace UDP)

2. **`EconetSocket` class** — models the optional Econet hardware upgrade

   Following the same pattern as `DiscControllerSocket` for the FDC, `EconetSocket` represents the physical hardware socket on the BBC motherboard. On the Model B this was a set of discrete chip positions (IC93 for the MC68B54, IC97 for the NMI gating flip-flop, etc.); on the Master series it was a pin header for a carrier board. The socket abstraction covers both.

   **When empty** (no Econet hardware fitted):
   - The `EconetSocket` is always present in the memory map, but returns open bus values when disabled
   - Reads to &FEA0-&FEBF return the previous bus value (fast 2MHz open bus — capacitance holds)
   - Reads to &FE18 return 0x00 (slow 1MHz open bus — pull-down resistors discharge)
   - Writes to both regions are ignored (fall through to no-op)
   - NMI is never asserted from the Econet source
   - The NFS ROM detects this state and skips NFS initialisation

   **When populated** (`--station N` specified):
   - Delegates ADLC register access (&FEA0-&FEA3 via `Mirror<0x03>`) to the `Mc6854`
   - Station ID register (&FE18) returns the configured station number
   - NMI gating logic (INTON/INTOFF) is active
   - ADLC is ticked on every 2MHz half-cycle

   **Key difference from `DiscControllerSocket`**: The FDC socket uses runtime polymorphism (`unique_ptr<DiscControllerInterface>`) because multiple controller types can be installed (8271, WD1770, Opus, Watford). The Econet socket always contains the same hardware (MC68B54 ADLC + glue logic), so it uses `unique_ptr<Mc6854>` (created by `enable()`, destroyed by `disable()`) with a boolean `enabled_` flag. No virtual dispatch needed for the ADLC; virtual dispatch is used only for the `NetworkBackend` interface.

   ```
   EconetSocket
   ├── adlc_                        // Mc6854 instance (always present, but only active when enabled)
   ├── enabled_                     // Whether Econet hardware is "fitted"
   ├── station_id_                  // Station number (from --station N)
   ├── nmi_enable_ff_               // INTON/INTOFF flip-flop (IC97 on Model B)
   ├── ADLC Region (MemoryMappedDevice for &FEA0-&FEBF)
   │   ├── read(offset)             // When enabled: adlc_.read(). When empty: last bus value (fast open bus)
   │   └── write(offset, value)     // When enabled: adlc_.write(). When empty: ignored
   ├── Station ID Region (MemoryMappedDevice for &FE18-&FE1F)
   │   ├── read(offset)             // When enabled: station_id_ + INTOFF. When empty: 0x00 (slow open bus)
   │   └── write(offset, value)     // Ignored (read-only register)
   ├── on_inton()                   // Called when &FE20 is accessed (INTON side-effect)
   ├── nmi_pending() -> bool        // enabled_ && nmi_enable_ff_ && adlc_.irq_output()
   ├── tick_rising()                // Delegates to adlc_ when enabled; no-op when empty
   └── tick_falling()               // Delegates to adlc_ when enabled; no-op when empty
   ```

3. **NMI gating (INTON/INTOFF)** — glue logic within `EconetSocket`

   The NMI gating is not part of the ADLC itself — it's external glue logic (IC97 on the Model B, a flip-flop on the Econet carrier board on the Master). It lives in the `EconetSocket` because it's part of the Econet hardware upgrade, not the base machine.

   - **INTOFF**: Triggered as a side-effect of reading the station ID register (&FE18). Clears `nmi_enable_ff_`. On real hardware, the address decoding for &FE18 drives the flip-flop's reset pin.
   - **INTON**: Triggered as a side-effect of accessing the Video ULA range (&FE20). Sets `nmi_enable_ff_`. This is a quirk of the BBC's address decoding — the Econet hardware taps the same select line.
   - On Master: INTOFF at &FE38, INTON at &FE3C (different addresses, same mechanism)
   - When INTON fires with a pending ADLC IRQ: NMI asserts on the next poll cycle

   **Integration with Video ULA**: The INTON side-effect must be triggered on *any* access to &FE20, not just Econet-specific accesses. This means the Video ULA's memory-mapped region needs a hook that notifies the `EconetSocket`. Options:
   - The hardware's `write` handler for &FE20 calls both `video_ula.write()` and `econet_socket.on_inton()`
   - Or the memory map routing explicitly invokes both

4. **Memory mapping** — two non-contiguous address regions, conditionally present

   The Econet hardware occupies two separate ranges in SHEILA:

   ```
   &FE18-&FE1F: Station ID register (+ INTOFF side-effect)
                Mapped to EconetSocket::read_station_id() / write_station_id()
                When empty: returns 0x00 (slow 1MHz open bus — pull-downs discharge)

   &FEA0-&FEBF: ADLC registers
                Mapped to EconetSocket::read_adlc() / write_adlc()
                When empty: returns last bus value (fast 2MHz open bus — capacitance holds)
   ```

   **Critical: these regions must produce correct open bus values when Econet is not fitted.** Unlike the `DiscControllerSocket` which always returns 0xFF when empty, the Econet regions have different open bus characteristics:
   - &FEA0 (fast 2MHz): previous bus value, NOT 0xFF
   - &FE18 (slow 1MHz): 0x00, NOT 0xFF

   Some code relies on these open bus values to detect whether Econet hardware is present. Getting this wrong would break hardware detection.

   **Approach**: Always include both regions in the memory map via `EconetSocket`, but have the socket produce correct open bus values when disabled:
   - `read_station_id()` when disabled: return 0x00 (1MHz open bus — pull-downs)
   - `read_adlc(offset)` when disabled: return the last bus value (fast open bus — capacitance)

   For the ADLC region, the socket needs access to the last bus value. Options:
   - Pass `last_bus_value` from the memory map's read dispatch as a parameter
   - Have `EconetSocket` track the bus value independently (less clean — duplicates state)
   - Use a reference/pointer to the memory map's `last_bus_value_` member

   When enabled, these regions delegate to the ADLC and station ID register normally.

   No bus stretching required — &FEA0-&FEBF is confirmed "fast" at 2MHz.

   **Note on &FE08-&FE0F (Serial ACIA)**: This range is also currently unmapped. It's unrelated to Econet but adjacent to the station ID range. Care needed to not overlap when adding the &FE18 region.

5. **Concepts for hardware detection** (following `DiscConcepts.hpp` pattern)

   ```cpp
   // Concept to detect if hardware has an Econet socket
   template<typename T>
   concept HasEconetSocket = requires(T& hw) {
       { hw.econet_socket } -> std::same_as<EconetSocket&>;
   };

   // Helper to check if Econet hardware is fitted at runtime
   template<typename T>
   bool econet_present(const T& hw) {
       if constexpr (HasEconetSocket<T>) {
           return hw.econet_socket.enabled();
       } else {
           return false;
       }
   }
   ```

   All three current hardware variants (Model B, Model B+, Model B with ROM/RAM board) have `EconetSocket econet_socket` as a member of their MemoryMap. The Master series (future) would also have it, with different INTON/INTOFF addresses.

6. **CTS/DCD logic**
   - DCD (SR2b5): low when network backend is connected (clock present), high otherwise
   - CTS: `!(backend.is_connected() && CR2b7_RTS)` — same logic as BeebEm
   - CTS positive-edge stored; cleared by Clear TX Status

7. **Integration with Machine::step()**
   - Tick `EconetSocket` on every 2MHz cycle (rising + falling edges) — no-op when empty
   - `kEconetNmiDeviceMask = 0x02` alongside existing `kDiscNmiDeviceMask = 0x01`
   - Aggregate NMI: disc at 1MHz + Econet at 2MHz (different polling rates)
   - Byte trickle rate: one TX/RX byte every 128 half-cycles (configurable, ~32us per byte)

**All items above are implemented.** Key implementation differences from the original design:
- `EconetSocket` uses `unique_ptr<Mc6854>` (not `optional<Mc6854>`) because the `Mc6854` constructor requires a `NetworkBackend&` reference at construction time.
- The ADLC open bus value for the &FEA0 region is provided via `set_last_bus_value_ptr()` — the pointer-to-member approach from the options listed above.
- The `HasEconetSocket` concept and `econet_present()` helper were not needed. The `EconetSocket` is a direct member of all MemoryMap variants, and its `enabled()` / `nmi_pending()` / `tick_*()` methods are all safe to call when the socket is empty.
- Byte trickle rate is 128 half-cycles (32us per byte), not 128 E-cycles (64us per byte) as stated in BeebEm's comments. Both rising and falling edges advance the byte timer, so 128 half-cycles = 64 full E-clock cycles.

### Completed: Econet Protocol Layer (originally Phase 2)

1. **Frame assembly/disassembly**
   - TX: accumulate bytes from FIFO into frame buffer; send on TxLast
   - RX: trickle bytes from frame buffer into FIFO; set AP on first byte, FC on last
   - No CRC needed (UDP provides checksums; real Econet CRC not simulated by BeebEm)

2. **Four-way handshake state machine** (10 states)
   - Fakes scout/ack phases locally (AUN only does data+ack)
   - Scout timeout: 5,000 cycles (~2.5ms) before generating fake ack
   - Watchdog timeout: 500,000 cycles (~250ms) force-resets hung transactions
   - Scout payload sizes depend on control byte (0x82→8 bytes, 0x83-0x85→4 bytes, other→0)
   - Immediate operations: single exchange without scout phase
   - Broadcasts: fire-and-forget, no handshake

3. **Pseudo flag fill**
   - Set on send and on seeing traffic for other stations
   - Cleared on receiving packet for us, on timeout, or on handshake completion
   - Drives SR1b3 (Flag Detected)
   - Timeout: 500,000 cycles (~250ms)

4. **Idle detection**
   - Idle = RX not reset AND FIFO empty AND no FV AND no pending data
   - Drives SR2b2 (Inactive Idle) when `Idle && !FlagFillActive`

**All items above are implemented.** Key implementation difference: the four-way handshake is implemented as a **decorator** (`FourWayHandshake`) that implements `NetworkBackend`, rather than being embedded in the `Mc6854`. The ADLC talks to `FourWayHandshake` using raw Econet frames; `FourWayHandshake` translates to/from typed AUN packets on the wrapped `AunBackend`. This keeps the ADLC pure hardware emulation and the protocol bridging in a separate, testable class.

The flag fill and idle detection are also in `FourWayHandshake` (via `is_receiving_flags()` and `is_expecting_frame()` on the `NetworkBackend` interface), not in `Mc6854`. The ADLC queries the backend for these signals and reflects them in the status registers.

### Completed: AUN Network Layer (originally Phase 3)

1. **UDP transport**
   - Socket management on port 32768 (configurable)
   - Non-blocking receive via `select()` with zero timeout
   - AUN packet encoding/decoding (8-byte header + payload)
   - Transaction handle: 32-bit sequence number incremented by 4
   - Broadcast via `SO_BROADCAST` socket option

2. **Local subnet discovery** (broadcast-based)
   - On startup, broadcast announcement: "station N at IP:port"
   - Listen for peer announcements, build dynamic peer table
   - Periodic re-announcements (handle stations joining/leaving)
   - No configuration needed for same-subnet peers

3. **Explicit address mapping** (for cross-subnet / bridge)
   - `--aun map=<net.stn@ip@port>` (repeatable) for explicit mappings
   - Static mappings take precedence over discovered peers

4. **Pi Econet Bridge compatibility**
   - A future transport extension would expose this; nothing built yet
   - Test with actual bridge hardware
   - Handle bridge-specific behaviors
   - Bridge provides access to real Econet stations

**Items 1, 2 and 3 are implemented.** Item 2 arrived as mDNS peer discovery rather than the AUN broadcast announcement originally sketched — see "AUN peer discovery via mDNS" above — so `--aun map=` is optional on a LAN and required only for hermetic tests, hosts without a working responder, and WAN deployments. The port in a `map=` entry must be specified explicitly (no default).

Item 4 (Pi Econet Bridge) is **implemented and tested**, though not as a dedicated extension: PiEconetBridge speaks AUN, so `AunBackend` talks to it directly. `integration_tests/pieb-aun/` runs a real bridge — with no Pi, no Econet HAT and no kernel module — and drives login and catalogue operations against its emulated fileserver. See `docs/discussion/pieconetbridge-aun-interop-testing.md`.

### Partially Complete: Configuration and Integration (originally Phase 4)

1. **Command-line options** — **Done**:
   - `--station <n>` - Enable Econet hardware and set station number (no flag = no Econet)
   - `--aun [port=<n>][:map=<net.stn@ip@port>]...` - AUN UDP transport with explicit station-to-IP mappings
   - `--piconet device_path=<path>` - Piconet USB-CDC bridge to a real Econet wire
   - The legacy `--aun-port`, `--aun-map`, and bare `--piconet <path>` flags have been removed; both transports flow through the generic extension dispatch (see "Econet Transport Extensions" above).

2. **Frontend integration** — **Not yet done.** Planned as part of the broader Econet integration work programme (see `docs/econet-integration.md`):
   - Preset integration (JSON format)
   - gRPC EconetService
   - Service discovery metadata
   - Python client wrapper
   - macOS client sidebar UI

3. **ROM management** — **Partially done.** NFS 3.34 ROM loading works via `--sideways 10:rom:acorn-nfs_3_34.rom`. ROM acquisition documentation is not yet written.

## Resolved: MC6854 FV/PSE Timing vs NFS ROM

### Summary

The integration test `test_econet_fileserver.cpp` connects to a real Acorn Level 3 File Server running in BeebEm on `127.0.0.1:32768`. The test boots a Beebium BBC with NFS 3.34, types `*NET` then `*.`, and expects "Who are you?" on the emulated screen. This now works correctly with push-time FV, inline refill, and a byte timer reset on FIFO read.

### Running the Integration Test

The test requires a BeebEm instance running as a file server. BeebEm's `Econet.cfg` must list station entries for both itself and Beebium:

```
AUNMODE 1
LEARN 0
AUNSTRICT 0
0 254 127.0.0.1 32768
0 101 127.0.0.1 10101
```

Run with:

```bash
cd build && cmake --build .
BEEBIUM_FILESERVER=0.254:127.0.0.1:32768 BEEBIUM_LOCAL_PORT=10101 ./tests/test_econet_fileserver
```

`BEEBIUM_LOCAL_PORT=10101` is essential — with LEARN off, BeebEm's `FindHost()` matches incoming UDP packets by both IP and port. An ephemeral port won't match the config entry and packets are silently discarded.

### The Two NFS ROM Code Paths

Two NFS ROM code paths have different expectations for when FV appears in SR2 relative to RDA:

#### Path 1: Scout Data Reading Loop ($9747-$976E)

This loop reads the body of an incoming scout frame, two bytes at a time:

```
$9747: LDA SR2         ; Check status
$974A: AND #$81        ; Test AP (b0) and RDA (b7)
$974C: BEQ $9744       ; Neither set → JMP $9A40 (discard scout)
$974F: BPL $9737       ; AP set but no RDA → check for errors
$9751: LDA RX_DATA     ; Read byte N
$9754: STA (ptr),Y
$9756: LDA SR2         ; Check status again
$9759: TAX             ; Save SR2 in X
$975A: BNE $9771       ; SR2 non-zero → scout completion path
$975C: INY             ; SR2=0 → loop (more data)
$975D: LDA RX_DATA     ; Read byte N+1
$9760: STA (ptr),Y
$9762: INY
$9763: CPY #$0C
$9765: BCS $9737       ; Buffer full → check/discard
$9768: LDA SR2         ; Check status for next iteration
$976B: AND #$81        ; Test AP|RDA
$976D: BMI $9751       ; RDA set → read next pair
$976F: BNE $9737       ; AP without RDA → check errors
$9771: ...             ; Scout completion
```

At $975A, the ROM checks whether SR2 is non-zero after reading a byte. FV must be visible here after the penultimate byte read so the branch to $9771 (scout completion) is taken.

#### Path 2: Reply Scout Handler ($9DB2-$9DFA)

When the NFS ROM receives a reply scout during a four-way handshake, a chain of NMI handlers processes it byte by byte:

```
$9DB2: LDA SR2         ; AP handler entry
$9DB5: AND #$01        ; Check AP
$9DB7: BEQ $9DCE       ; No AP → error path
$9DB9: LDA RX_DATA     ; Read byte 0 (dest station)
$9DBC: ...             ; Install $9DC8 as next handler

$9DC8: LDA SR2         ; Continuation handler
$9DCB: AND #$80        ; Check RDA (b7)
$9DCD: BEQ $9DCE       ; No RDA → error path
$9DCF: LDA RX_DATA     ; Read byte 1 (source network)
$9DD2: ...             ; Install $9DE3 as next handler

$9DE3: LDA SR2         ; Validation handler
$9DE6: BMI $9DEB       ; RDA (b7) set → read bytes
$9DE8: JMP error       ; No RDA → error
$9DEB: LDA RX_DATA     ; Read byte 2 (source station)
$9DEE: STA $0D3D
$9DF1: LDA RX_DATA     ; Read byte 3 (source network)
$9DF4: STA $0D3E
$9DF7: LDA SR2
$9DFA: AND #$02        ; Check FV (b1)
$9DFC: BEQ error       ; No FV → error
```

At $9DE3, the handler needs RDA visible (not masked by FV via PSE) to read the remaining two bytes. FV is only checked at $9DFA after all bytes are read.

### How the Conflict is Resolved

The apparent conflict was between push-time FV (needed by Path 1) and FV masking RDA via PSE (breaking Path 2). The resolution is that **inline refill** and **byte timer reset** together satisfy both paths without any PSE model changes:

1. **Push-time FV with inline refill** handles Path 1: when the CPU reads the penultimate byte at $9751, inline refill pushes the last byte from the frame buffer, setting FV immediately. At $9756, SR2 is non-zero (FV set at P1), so BNE at $975A branches to $9771.

2. **Inline refill chain** handles Path 2: the reply scout handler reads bytes consecutively via NMI re-entry ($9DB2→$9DC8→$9DE3). Each read triggers inline refill of the next byte. Byte 0 read → refills byte 1 (RDA). Byte 1 read → refills byte 2 (RDA). Byte 2 read → refills byte 3/LAST (FV set, but byte 3 is already in the FIFO). At $9DE3, the NMI handler sees RDA because the inline refill from the previous read already pushed byte 2 into the FIFO. FV isn't set until byte 3 is pushed during the byte 2 read, which happens *after* the $9DE3 check.

3. **Byte timer reset on FIFO read** (`byte_timer_ = 0` in `read_rx_fifo()`) prevents the byte timer from firing during the NFS ROM's tight polling loop. Without this reset, the byte timer could push the last byte mid-loop (e.g. between the $9751 read and the $9756 SR2 check), setting FV at an unexpected point and causing the loop to exit to the error handler at $9737 instead of the scout completion path at $9771. Resetting the timer on each read ensures a full byte period (128 half-cycles) must elapse before the next timer-driven push — much longer than the ~40 half-cycle loop iteration.

### Implementation Details

The `Mc6854.hpp` implementation uses:

- **Push-time FV**: `fv_deferred_` set immediately in `rx_push_one_byte()` when the LAST-flagged byte is pushed
- **Inline refill**: `read_rx_fifo()` pushes one byte from the frame buffer after each CPU read, keeping the FIFO populated
- **Byte timer reset**: `read_rx_fifo()` resets `byte_timer_ = 0` after each valid read, preventing timer-driven pushes from interleaving with the ROM's fast polling
- **Stateless PSE cascade**: `apply_pse_filter()` evaluates P1→P4 on each call; FV at P1 masks RDA at P4

### How BeebEm Differs

For reference, BeebEm uses a different approach that also works:

1. **No inline refill**: FIFO filled exclusively via byte timer (one byte per 128 CPU cycles)
2. **FV from FIFO output position**: FV set when last byte reaches output slot, not on entry
3. **PSE floor model**: `sr2pse` counter advances on `CLR_RX_ST`, resets when no SR2 bits active
4. **sr2pse resets when FIFO empties**: Between timer ticks, FIFO can empty, resetting the cascade

Beebium's inline refill + byte timer reset approach is functionally equivalent but avoids the need for BeebEm's more complex PSE floor model.

### FourWayHandshake Port-0 Classification

A separate issue (already fixed in the current branch): the `FourWayHandshake` classified all port-0 frames as Immediate operations, but BeebEm treats port-0 with control bytes 0x02-0x05 (POKE, JSR, UserProc, OSProc) as Unicast scouts with extra payload. Fixed in `FourWayHandshake.hpp` by checking the masked control byte:

```cpp
uint8_t masked_ctrl = ctrl & 0x7F;
bool is_port0_unicast = (masked_ctrl >= 0x02 && masked_ctrl <= 0x05);
if (port == 0x00 && !is_port0_unicast) {
    // Immediate operation
}
```

### NFS ROM Code Reference

Key NFS 3.34 ROM routines for understanding the ADLC interaction:

| Address | Function |
|---------|----------|
| $96DC | `sub_c96dc`: Full ADLC reset (CR1=$C1, CR4=$1E, CR3=$00) |
| $96EB | `sub_c96eb`: RX listen mode (CR1=$82, CR2=$67 with CLR_RX_ST) |
| $96F6 | Initial RX scout handler: checks AP, reads dest station, installs $9715 |
| $9715 | Second byte handler: checks RDA, reads source network, installs $9747 |
| $9747 | Scout data reading loop: reads bytes in pairs, checks SR2 between reads |
| $9771 | Scout completion: turns off PSE (CR2=$84), checks FV and RDA, reads last byte |
| $9A40 | Discard path: calls $96EB (CLR_RX_ST), installs idle handler $96F6 |
| $9DB2 | Reply scout AP handler: checks AP, reads byte 0, installs $9DC8 |
| $9DC8 | Reply continuation: checks RDA, reads byte 1, installs $9DE3 |
| $9DE3 | Reply validation: checks RDA, reads bytes 2-3, then checks FV |

All of these routines are in `disassembly/nfs_334_v2_96dc_9fff_adlc_nmi_handlers.asm` (included from `disassembly/nfs_334_v2.asm`, generated by `disassembly/nfs_334_v2.py`). The disassembly is split by address range:

| File | Range | Contents |
|------|-------|----------|
| `nfs_334_v2_preamble.asm` | — | Constants, workspace definitions |
| `nfs_334_v2_8000_84ff_header_dispatch_init.asm` | $8000-$84FF | ROM header, dispatch tables, init |
| `nfs_334_v2_8500_8dff_filing_system.asm` | $8500-$8DFF | Filing system operations |
| `nfs_334_v2_8e00_96db_cli_and_commands.asm` | $8E00-$96DB | CLI commands (*NET, *I AM, etc.) |
| `nfs_334_v2_96dc_9fff_adlc_nmi_handlers.asm` | $96DC-$9FFF | **ADLC register access, NMI handlers, four-way handshake** |
| `nfs_334_v2_appendix.asm` | — | Data tables, string constants |

## Open Questions

1. ~~**Timing accuracy**~~ **Resolved.** Beebium takes a cycle-accurate approach on the E-clock domain (2MHz, both phases), departing from BeebEm's poll-based model. The ADLC is ticked every 2MHz half-cycle with status bits updated synchronously on E-clock edges and NMI asserting on the same cycle as the triggering condition. The serial bit-level domain (TxC/RxC) is abstracted to byte-level timed events since Beebium uses AUN-over-UDP, not a real Econet wire. Byte trickle rate remains configurable (default 128 half-cycles per byte, ~32 us). See "Cycle-Accurate ADLC Design" section for full details.

2. ~~**Clock detection**~~ **Resolved.** DCD (SR2b5) reflects `!backend.is_connected()`: high when disconnected (no clock), low when connected. CTS reflects `!(backend.is_connected() && CR2b7_RTS)`. The NFS ROM polls DCD during boot and reports "No Clock" when DCD is high. This is implemented and verified — see `test_boot_econet.cpp` for "No Clock" boot test.

3. **Multi-network support**: The current implementation supports arbitrary network numbers via `--aun map=net.stn@ip@port` where `net` can be 0-255. Network number 0 is the default for local networks. MASSAGENETS (bit-7 translation) is not implemented. Sufficient for current needs.

4. ~~**ROM licensing**~~ **Resolved.** The original copyright holder (Acorn Computers) is defunct. While the ROMs are technically still under copyright, there is no entity to enforce it. Widespread retro-computing community practice (distribution via mdfs.net, stardot.org.uk, etc.) demonstrates essentially zero risk. Beebium does not currently bundle NFS/ANFS ROMs but could do so if convenient.

5. ~~**Broadcast announcement format**~~ **Resolved differently.** Rather than an AUN broadcast announcement, peer discovery is done with mDNS / DNS-SD under the vendor-neutral `_aun._udp` service type, reusing the discovery machinery Beebium already had. See `docs/discussion/aun-mdns-peer-discovery.md`.

6. **Self-send prevention**: Not explicitly handled. The AunBackend will send packets to any configured peer address, including one that maps to the local station. In practice this hasn't caused problems because Beebium instances use different UDP ports.

7. **NACK handling**: Beebium follows BeebEm's pragmatic approach — NACKs are not explicitly handled. The `FourWayHandshake` watchdog timeout resets hung transactions regardless of the cause.

8. ~~**Immediate operation data sizes**~~ **Resolved.** Verified against the NFS 3.34 ROM disassembly and BeebEm's implementation. Implemented in `FourWayHandshake::scout_payload_size()`: control byte 0x02 (POKE) → 8 bytes, 0x03-0x05 (JSR, UserProc, OSProc) → 4 bytes, all others → 0 bytes.

## Design Considerations

### Station Configuration (Avoiding BeebEm's Sequential Model)

BeebEm uses a sequential consumption model where each new instance takes the next entry from `Econet.cfg`. This creates launch-order dependencies and makes it awkward to restart a specific instance.

**Adopted approach for Beebium:**

`--station N` both enables Econet hardware AND sets station number:
```
beebium-model-b --station 254    # File server, Econet enabled
beebium-model-b --station 1      # Workstation
beebium-model-b                  # No Econet hardware fitted
```

This mirrors physical hardware — you either have the Econet interface fitted or you don't. The NFS ROM auto-detects hardware presence and behaves accordingly.

**Peer configuration:**
Peers are discovered over mDNS on a LAN; `--aun map=` remains for hermetic tests, hosts without a working mDNS responder, and WAN deployments.

**Future enhancements:**
- Preset integration (JSON format) for Econet configuration — see `docs/econet-integration.md`
- gRPC-based runtime configuration (add/remove peers, enable/disable Econet)
- Local subnet discovery via broadcast

### Usage Examples

**Connecting to a BeebEm file server (tested and working):**
```bash
# Start BeebEm with Econet.cfg containing:
#   AUNMODE 1
#   LEARN 0
#   AUNSTRICT 0
#   0 254 127.0.0.1 32768
#   0 101 127.0.0.1 10101

# Beebium workstation connecting to BeebEm file server
beebium-model-b --station 101 --aun port=10101:map=0.254@127.0.0.1@32768

# On the workstation:
# *NET
# *I AM SYST
# *.
```

**Two Beebium instances on the same machine:**
```bash
# Terminal 1: File server (station 254, port 32768)
beebium-model-b --station 254 --aun port=32768:map=0.1@127.0.0.1@32769

# Terminal 2: Workstation (station 1, port 32769)
beebium-model-b --station 1 --aun port=32769:map=0.254@127.0.0.1@32768
```

**Cross-subnet with explicit mapping:**
```bash
# Workstation connecting to a remote file server
beebium-model-b --station 1 --aun map=0.254@192.168.2.50@32768
```

**No Econet (DFS only):**
```bash
# Without --station, no Econet hardware is fitted
beebium-model-b --drive0 games.ssd
```

## References

### Documentation (OCR'd text available in docs/manuals_text/)

- **MC68B54 ADLC Datasheet** (Motorola) - `docs/datasheets/MOTOROLA MICROPROCESSORS DATA MANUAL 6854 section.pdf`
  - Complete register specifications, timing diagrams, programming considerations
  - Key insight: Status priority - test lowest priority conditions first (most frequent)
  - Stored vs Present status: DCD, Rx Abort, Rx Idle are OR of stored + present conditions
  - FIFO: 3-byte TX and RX FIFOs with pointer-based frame boundary tracking
  - E clock constraint: period between successive E pulses must be less than period of RxC/TxC
  - Key diagrams: Block diagram (Fig 1), Bus timing (Fig 6), TX/RX state diagrams (Fig 8a/8b), Status priority tree (Fig 10)
  - Bus timing table: MC68B54 cycle time min 0.5 us (= 2MHz max), E Low min 210ns, E High min 220ns

- **AUN Manager's Guide** (Acorn, 1992) - `docs/manuals_text/AUN_Managers_Guide/full_text.md`
  - AUN uses UDP + proprietary two-way handshake (not Econet's four-way)
  - IP address format: `1.network.net.station` (Class A, netmask &FFFF0000)
  - Uses RIP for routing, RevARP for client address discovery
  - Default isolated network address: `1.0.128.station`

- **DNFS Manual** (Acorn, 1984) - `docs/manuals_text/DNFS_Manual/full_text.md`
  - DNFS ROM contains both DFS 1.20 and NFS 3.60
  - Keyboard switch 1 selects DFS vs NFS at boot
  - Auto-detects Econet/DFS hardware presence

- **BBC Micro Advanced User Guide, Chapter 25** - `docs/manuals_text/Advanced_User_Guide/25_25._Floppy_Disc_and_Econet.md`
  - ADLC register addresses (&FEA0-&FEA3), station ID register (&FE18)
  - "The 68B54 ADLC is the central component in the Econet Interface circuit"
  - NMI auto-enabled by reading station ID register

- **BBC Micro Advanced User Guide, Chapter 28** - `docs/manuals_text/Advanced_User_Guide/28_28._The_One_Megahertz_bus.md`
  - 1MHz bus timing, bus stretching mechanism, double-accessing problem
  - NNMI pin (pin 6) connected directly to 6502 NMI input, pulled up to +5V with 3K3 resistor
  - NMIs triggered on negative-going edges

- **BBC Master New Advanced User Guide, Chapter 11** - `docs/manuals_text/BBC_Master-New-Advanced-User-Guide/11_11._Hardware.md`
  - Master-specific INTON/INTOFF addresses (&FE38/&FE3C vs Model B's &FE18/&FE20)

- **BBC Master CMOS RAM layout** (from BeebEm `Rtc.cpp` lines 73-77)
  - Byte 0x0E: Econet station number (`*SETSTATION`)
  - Bytes 0x0F-0x12: File/printer server station and network numbers
  - Protected byte — cannot be written via OSBYTE 162; requires `*SETSTATION` or direct VIA access

- **AUN Manager's Guide** - `docs/manuals_text/AUN_Managers_Guide/full_text.md` (lines 2485-2522)
  - `*SETSTATION` command documentation (sets station number in CMOS, range 2-254)

- Econet Advanced User Guide (Acorn, 1988)

### Code References

- BeebEm Windows (primary reference): `/Users/rjs/Code/beebem-windows/Src/Econet.cpp` + `Econet.h`
  - Most recently maintained version (~2720 lines)
  - Full AUN support (added 2009), 10-state handshake FSM
  - ADLC integration via `BeebMem.cpp` (INTON/INTOFF, register read/write)
  - NMI handling in `6502core.cpp` (EconetPoll called after each instruction)
- BeebEm macOS: `/Users/rjs/Code/beebem-mac/Src/Econet.cpp`
  - Structurally identical to Windows; `#ifdef __APPLE__` for platform differences
- MAME: `/Users/rjs/Code/mame/src/devices/machine/mc6854.cpp` + `mc6854.h`
  - ~985 lines; cycle-accurate ADLC with both bit-level and frame-level interfaces
  - Clean `uint16_t` FIFO entries with metadata in upper bits (adopted for Beebium)
  - 7-state frame field machine tracking address/control/data phases (adopted for Beebium)
  - Full bit-level RX: zero deletion, flag detection, abort detection, shift register
  - Extended addressing (AEX) and extended control (CEX) field handling
  - Unimplemented: PSE, stored/present status, CRC (hardcoded), loop mode, NRZ
  - Used for Thomson nano-network extension (up to 32 computers at 500 Kbps)
- Pi Econet Bridge: https://github.com/cr12925/PiEconetBridge

### Beebium Integration Points

- `src/core/include/beebium/BusStretching.hpp` — Econet at index 5 marked "fast" (no stretching), confirms 2MHz
- `src/core/include/beebium/Machine.hpp` — `Machine::step()` tick ordering, NMI polling, `kDiscNmiDeviceMask`
- `src/core/include/beebium/NmiAggregator.hpp` — `NmiSource` concept, `NmiBinding`, multi-source NMI aggregation
- `src/core/include/beebium/ClockConcepts.hpp` — `ClockSubscriber`, `StaticRateSubscriber` concepts
- `src/core/include/beebium/ClockBinding.hpp` — `should_tick()` based on static/dynamic clock rate
- `src/core/include/beebium/Via6522.hpp` — Existing 2MHz peripheral model (both edges, both phases)

### NFS ROM Internals

- **NFS Workspace Layout** (J.G. Harston): https://mdfs.net/Misc/Source/Acorn/NFS/NFSWorkSp
  - &00C0-&00CB: 12-byte NetTx and NetRx control block for NetFS_Op
  - (&9A): Current NetTx control block pointer
  - (&A0): Block to be transmitted (NMI workspace)
  - (&9E): Private workspace 2 — receive buffers at offsets &00, &0C, &18, &24 (12-byte structures)
  - (&A4): Open port buffer pointer

- **Page &0D Usage** (J.G. Harston): https://mdfs.net/Misc/Source/Acorn/NFS/PageD
  - &0D3D-&0D40: Incoming scout data (src_stn, src_net, ctrl, port)
  - &0D4A: Control status byte
  - &0D4B-&0D4C: Address in DNFS ROM of handler for next NMI
  - &0D62: OSWORD busy flag (0=busy, &80=idle)

### Online Resources

- J.G. Harston's Econet pages: http://mdfs.net/Docs/Comp/Acorn/Econet/
- Acorn NFS Disassembly https://acornaeology.uk/acorn-nfs/
- BeebWiki Econet documentation
- StarDot forums (retro computing community)
