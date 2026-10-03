# Dynamic Station Configuration Protocol (DSCP)

Speculative design for automatic Econet station number assignment alongside
AUN, informed by analysis of PiEconetBridge's architecture and BeebEm's
station self-assignment problems.

**Status (October 2026): not implemented; superseded for the Beebium-only
case.** No DSCP server, wire format or `dscp:` station form exists. Automatic
station numbering was built instead without a coordinator (#67, #160,
#161): `--station auto[:lo-hi]` (default range 1-253, or a preset's
`"econet": {"station": "auto"}`) makes the AUN transport claim a number with
its own `_aun._udp` announcement and climb past any earlier-bound station it
discovers, starting one past the last number this host allocated (a per-host
hint file, `BEEBIUM_AUN_AUTO_STATE_FILEPATH`). It needs no server process,
respects map-file and `map=` stations, and arbitrates same-instant races
only best-effort; the residual clash surfaces as an ordinary station
collision (#147). So `--station auto` below means the proposed DSCP
behaviour, not today's. See `docs/networking.md`, "Choosing a free station
number at launch". The proposal remains relevant for mixed networks with
real hardware or non-mDNS emulators, which the decentralised scheme cannot
see.

Originally: early exploration, not committed to implementation.

---

## Problem Statement

When multiple Beebium instances (or a mix of Beebium, BeebEm, and real
hardware) share an Econet, station numbers must be unique. Today this is
managed manually:

- Real hardware: DIP switches or CMOS RAM (fixed at configuration time)
- Beebium: `--station N` on the command line, or preset file (now also
  `--station auto`, the decentralised mDNS scheme in the status note above)
- BeebEm: `Econet.cfg` or their proprietary self-assignment mechanism

Manual assignment breaks down when:

- Users launch several instances without coordinating numbers
- Instances come and go dynamically (e.g., classroom or demo setups)
- Mixed networks include both emulators and real hardware with pre-assigned
  station numbers that must be respected

BeebEm's self-assignment (see `docs/local-beebem-econet-lessons.md` section 0, a local, untracked
note)
is purely emulator-to-emulator, incompatible with real hardware, uses a
non-standard port formula, and has destructive collision resolution. It
demonstrates the need but not a viable solution.

## Design Principle: Alongside AUN, Not Inside It

AUN is a transport protocol for Econet frames over UDP. Station assignment is
a network management concern. These must be separate:

- **AUN** carries Econet frames (types 1-6) between stations with known
  addresses. It assumes station numbers are already assigned.
- **DSCP** assigns station numbers before AUN traffic begins. It runs as an
  IP-native protocol alongside AUN, using its own transport (not AUN packet
  types).
- **mDNS** discovers peers and advertises station metadata. It runs alongside
  both AUN and DSCP.

This separation means AUN interop with other implementations (BeebEm, RISC OS,
PiEconetBridge) is never compromised by Beebium-specific extensions.

## The Station Number Landscape

A DSCP server must have awareness of the full station number space, including
stations it did not assign:

| Station type | How DSCP learns about it |
|---|---|
| Real hardware (DIP switches) | Wire-side enumeration (see below) |
| Statically configured emulators | Registered as reservations |
| PiEconetBridge local services | Registered as reservations |
| DSCP-assigned emulators | Assigned by DSCP itself |

### Wire-Side Enumeration

Real BBC Micros don't announce themselves. They respond to traffic but don't
initiate it at startup (beyond NFS boot requests to a known fileserver). To
discover which station numbers are in use on a physical Econet, the DSCP
server would need to actively probe:

**Immediate Machine Type (control byte 0x88)**: The standard Econet immediate
operation that queries a station's machine type. Every Econet station that has
its NMI enabled will respond. PiEconetBridge already handles these for its
local stations (returning 0xEE for emulated services). A DSCP server could
sweep station numbers 1-254 with Machine Type queries on the wire side to
build a map of occupied numbers.

**Caveats:**

- A station that is powered off or has Econet disabled won't respond. The
  server can only see currently-active stations.
- The sweep takes time (~250ms timeout per non-responding station in the worst
  case, though a shorter timeout is reasonable for a local wire).
- Stations that power on after the sweep will be missed until the next sweep
  or until they generate traffic that the server observes passively.
- Passive observation (watching all wire traffic for source addresses) can
  supplement active sweeps. PiEconetBridge already does this via its
  `wire.stations[]` bitmap, which is updated whenever traffic is seen from a
  station.

## Where Could a DSCP Server Run?

### Option A: Standalone IP Service

A lightweight server process (could be a Beebium utility, or a separate
daemon) that:

1. Listens on a well-known port (or is discoverable via mDNS)
2. Accepts DSCP requests from Beebium instances
3. Maintains a station number allocation table
4. Optionally connects to PiEconetBridge (via AUN or pipe) to observe
   wire-side station traffic

**Pros:** Simple, can run anywhere (macOS, Linux, Docker), no hardware
dependency. Could be a gRPC service (Beebium already has gRPC infrastructure)
or a simple UDP protocol.

**Cons:** Has no direct visibility of real Econet stations unless it has a
connection to a bridge.

### Option B: PiEconetBridge PIPESERVER Integration

PiEconetBridge's PIPESERVER mechanism creates a named-pipe IPC channel for
external programs to act as Econet stations. In PASSTHRU mode, the pipe client
has full control over ACK/NAK handling and sees all traffic types.

A DSCP server could run as a PIPESERVER client on a designated station number
(e.g., net.253), receiving Machine Type queries and responding with DSCP
allocation information. However, PIPESERVER is designed for programs that *are*
Econet stations, not for programs that *manage* Econet stations. The DSCP
server's role is administrative, not participatory.

More naturally, the DSCP server would be a companion process to PiEconetBridge
that:

- Reads the bridge's station bitmap (via shared memory, config file, or a
  management API) to know which wire stations are active
- Has its own IP-side interface for Beebium DSCP requests
- Tells the bridge about newly-assigned stations (so the bridge can add them
  to its routing table)

PiEconetBridge's `DYNAMIC` network feature (see analysis below) already does
something adjacent: when unknown AUN traffic arrives, the bridge allocates a
station number from a configured pool. But this is a transparent server-side
routing decision — the client is never informed of its assigned number. DSCP
would be proactive (client requests before sending any Econet traffic) and
explicit (client receives and configures the assigned number).

### PiEconetBridge's Dynamic Allocation: What It Is and Isn't

PiEconetBridge's `DYNAMIC <net>` config creates a pool of station numbers.
When an AUN packet arrives from an IP:port not in the bridge's configuration,
the bridge searches the pool for a free slot (never used, or last-traffic
timestamp exceeds the expiry — default 1 hour). If found, it binds that
IP:port to the slot's station number in its internal routing table
(`econet-hpbridge.c` lines 3535-3548).

**This is not a protocol — it's transparent source-address rewriting.**

From that point on, the bridge rewrites the source address on all traffic from
that IP:port to the allocated net.stn (lines 3626-3627:
`incoming.p.srcnet = source_device->net;
incoming.p.srcstn = source_device->aun->stn;`). The client's self-declared
station number in its outgoing AUN packets is silently discarded.

**The client has no idea what number was assigned.** The AUN client (BeebEm,
Beebium, or a RISC OS machine) sends packets with whatever source station
number it believes it has. The bridge ignores that and substitutes the
dynamically-allocated number. The client continues using its self-assigned
number locally — its NFS ROM reads it from the station ID register, its
outgoing frames carry it — but from the perspective of every other station on
the network, this client's station number is whatever the bridge decided.

**This works for routing but breaks identity:**

- Fileserver login works (the fileserver sees the bridge-assigned number,
  replies to that number, the bridge routes back to the right IP:port)
- But `*WHOAMI` on the client shows the client's local number (read from
  the station ID register), not the bridge-assigned number
- And `*STATIONS` on other machines shows the bridge-assigned number
- Any protocol that relies on the client knowing its own station number
  (e.g., comparing its number against an address in a received frame)
  will see a mismatch

The client can discover its "real" (bridge-assigned) number only by doing
a Bridge WhatNet query (port 0x9C, control byte 0x82) — the standard
Econet bridge protocol. But this only returns the network number, not the
station number. There is no standard mechanism for a client to ask the
bridge "what station number have you assigned me?"

**BeebEm's handling:** BeebEm's master branch doesn't perform WhatNet
queries. The `econet-reworking` branches added WhatNet interception (commit
725f344), but only to learn the network number, not the station number.
The self-assignment has already committed to a station number before any
bridge communication occurs.

**The spoofed-BYE problem:** When a dynamic slot is reused, the bridge
originally sent a spoofed `*BYE` (logout) to all known fileservers on
behalf of the old occupant, to prevent stale sessions. This was disabled
(commented out in the source) because it "confuses the hell out of BeebEm"
— the BeebEm instance receives unexpected fileserver replies to a logout
it never initiated.

**Conclusion:** PiEconetBridge's dynamic allocation is a transparent
NAT-like mechanism. It solves the bridge's routing problem (getting packets
to the right IP:port) without solving the client's identity problem (knowing
your own station number). It requires nothing of the client — which is both
its strength (any AUN implementation works) and its weakness (the client's
local station number is a fiction). It is not a station configuration
protocol.

This is exactly the gap DSCP fills: a proper request/response protocol where
the client explicitly asks for a number, receives it, and configures its
local station ID register with the assigned value *before* enabling the ADLC
and sending any Econet traffic. No identity mismatch, no silent rewriting,
no NAT-like confusion.

### Option C: Custom Bridge With DSCP Built In

Build a Beebium-native bridge process that:

- Uses the same Raspberry Pi Econet HAT hardware as PiEconetBridge (via the
  `/dev/econet-gpio` kernel module)
- Provides wire-side Econet connectivity (like PiEconetBridge)
- Has DSCP as a native capability (not bolted on)
- Exposes a gRPC management API consistent with Beebium's conventions

This is the most ambitious option. It would replace PiEconetBridge for
Beebium users but is a large undertaking. The PiEconetBridge kernel module
interface is documented (ioctl commands for station map, flag fill, read/write
of AUN-formatted packets via `/dev/econet-gpio`) so the hardware interface is
understood.

**Pros:** Full control, native gRPC, no pipe/IPC impedance mismatch, can
implement DSCP as a first-class feature.

**Cons:** Substantial development effort. PiEconetBridge already works.
Duplicates routing, fileserver, and bridge protocol logic.

### Recommendation

**Start with Option A** (standalone IP service). It has no hardware
dependency, can be tested entirely in software, and the DSCP protocol design
is the same regardless of where the server runs. Wire-side enumeration can be
added later via Option B or C.

Option B (PIPESERVER integration with PiEconetBridge) is the natural next
step for mixed hardware/emulator networks. The pipe protocol is simple and
well-documented.

Option C is premature but worth keeping in mind. If PiEconetBridge's
architecture proves limiting (it's a large C codebase with no test suite),
a Beebium-native bridge could be a long-term goal.

## Local Auto-Launch Model

For the common case — several emulators running on one or a few machines,
no PiEconetBridge — the DSCP server should be a small standalone program
(`beebium-dscp`) that can be launched on demand.

### Discovery and On-Demand Start

1. User specifies `--station auto` (or preset/gRPC equivalent)
2. Beebium browses mDNS for `_beebium-dscp._tcp`
3. **If found:** connect via gRPC, send `AllocateStation`, configure with
   the response
4. **If not found:** launch `beebium-dscp` as a child process, wait for its
   mDNS advertisement to appear, then connect

The mDNS advertisement serves as both a discovery mechanism and a distributed
lock: the first Beebium instance to need DSCP starts the server; subsequent
instances on the same LAN discover it via mDNS and connect to the existing
one. No explicit coordination is needed — mDNS handles the "is there already
a server?" question.

### Server Lifecycle

The DSCP server is a lightweight coordination process, not a heavyweight
daemon. Options for lifecycle management:

- **Outlive clients:** The server stays running until all leases have expired
  and no clients remain, then exits. The next `--station auto` instance
  starts a new one.
- **Fire-and-forget:** The server runs indefinitely once started. Harmless
  on a development machine; could be registered as a launchd/systemd service
  for long-running setups.
- **Embedded in the first instance:** The first Beebium instance that needs
  DSCP could host the server in-process (a gRPC service alongside its
  existing ones). This avoids a separate binary but couples the server
  lifetime to one emulator instance — if that instance quits, all leases
  need to be re-acquired. Probably not worth the complexity.

The separate-binary approach is simplest and most robust. The binary is
small — it needs no emulation code, just:

- A UDP socket for DSCP requests/responses
- An mDNS advertisement (`_beebium-dscp._udp`)
- An in-memory allocation table (a `std::map` of station numbers to lease
  records, with a timer for expiry)
- Optionally, a simple persistence file so allocations survive a restart
  (not critical for the MVP)

No gRPC, no protobuf. The only shared dependency with Beebium is the mDNS
library (already used by `src/discovery/`). This makes the binary trivially
small and keeps the door open for other emulators to implement DSCP clients
without adopting Beebium's dependency stack.

### Build Target

`beebium-dscp` would be a new CMake target alongside the existing server
executables (`beebium-model-b`, `beebium-model-b-plus`, etc.). It shares
only the mDNS infrastructure from `src/discovery/` and has no dependency on
the emulation core, gRPC, or protobuf.

### Client-Side Integration

The `--station` argument accepts several forms:

| Form | Behaviour |
|------|-----------|
| `--station 42` | Static assignment (existing behaviour) |
| `--station auto` | Discover DSCP server via mDNS, auto-launch if absent |
| `--station dscp:192.168.1.5:9853` | Use DSCP server at explicit IP:port |
| `--station dscp:192.168.1.5` | Explicit IP, default DSCP port |

The `dscp:` prefix bypasses mDNS discovery entirely, which is useful when:

- mDNS is not available (some corporate networks block multicast)
- The DSCP server is on a different subnet (mDNS is link-local)
- Testing or debugging (point at a specific server instance)

The `auto` flow:

1. Browse mDNS for `_beebium-dscp._udp` (with a short timeout, e.g., 2s)
2. If not found, attempt to launch `beebium-dscp` (find it on `$PATH` or
   alongside the server binary)
3. Re-browse mDNS (the server advertises immediately on startup)
4. Send a UDP Allocate request to the discovered server
5. Configure `EconetSocket::enable()` with the assigned station number
6. Configure `AunBackend` peer table from the peer list in the response
7. Periodically send Renew requests (e.g., every 30 minutes)
8. Send a Release request on graceful shutdown

The `dscp:<host>:<port>` flow skips steps 1-3 and goes directly to step 4.

If the DSCP server cannot be found, launched, or reached, `--station auto`
(or `dscp:...`) fails with a clear error message. No silent self-assignment,
no port-number tricks.

The same forms would work in preset files:

```json
{
  "econet": {
    "station": "auto"
  }
}
```

```json
{
  "econet": {
    "station": "dscp:192.168.1.5:9853"
  }
}
```

Any other emulator (BeebEm, b-em, etc.) could implement the DSCP client
with a few dozen lines of socket code. No Beebium-specific libraries needed.
The `dscp:<host>:<port>` form is especially easy — no mDNS dependency at all,
just send a UDP datagram and parse the response.

## DSCP Protocol Sketch

### Transport

The protocol must be lightweight enough for any emulator to implement with
nothing more than BSD sockets. gRPC is too heavy a dependency — it would
discourage adoption by other emulators (BeebEm, b-em, etc.). A simple
UDP request/response protocol is sufficient: the data volumes are tiny
(a few tens of bytes per message), there are no streaming requirements,
and UDP's connectionless nature matches the fire-and-forget lease model.

The DSCP server is discoverable via mDNS (service type
`_beebium-dscp._udp`). The mDNS TXT record includes the UDP port.
Any emulator with mDNS browse capability (or a hard-coded/user-configured
server address) can participate.

For Beebium specifically, the internal `--station auto` implementation can
use DSCP over UDP directly — no gRPC dependency in the DSCP client path.
Beebium's gRPC EconetService remains the API for external tools to
*observe* Econet state, but DSCP is a separate, simpler protocol.

### Wire Format

Fixed-format binary packets. No JSON, no protobuf, no TLVs for the MVP.
The protocol is simple enough that a fixed layout is clearer and easier to
implement in any language (C, C++, Python, 6502 assembly for the wire-side
variant).

All multi-byte integers are little-endian (matching AUN convention).

#### Common Header (4 bytes)

```
Offset  Size  Field
0       1     Magic (0xD5 — 'D'SCP, and a nod to the Econet flag byte)
1       1     Version (0x01)
2       1     Operation (see below)
3       1     Status (0x00 in requests; result code in responses)
```

#### Operations

| Op | Name | Direction | Description |
|----|------|-----------|-------------|
| 0x01 | Allocate | Request | Request a station number |
| 0x81 | Allocate | Response | Assigned station number |
| 0x02 | Release | Request | Release a station number |
| 0x82 | Release | Response | Acknowledgement |
| 0x03 | Renew | Request | Renew a lease |
| 0x83 | Renew | Response | Renewed lease |
| 0x04 | List | Request | Query allocation table |
| 0x84 | List | Response | Allocation table |

Responses have bit 7 set (0x80 OR'd with the request op).

#### Status Codes

| Code | Meaning |
|------|---------|
| 0x00 | OK |
| 0x01 | No stations available |
| 0x02 | Station already in use |
| 0x03 | Unknown lease (release/renew for unrecognised client) |
| 0x04 | Protocol error |

#### Allocate Request (header + 8 bytes = 12 bytes total)

```
Offset  Size  Field
4       1     Preferred station (0 = no preference)
5       1     Network (0 = local)
6       2     AUN port (client's AUN listen port, for peer table)
8       4     Lease duration in seconds (0 = server default)
```

The server infers the client's IP address from the UDP source address.
A 4-byte client nonce or cookie could be added if lease tracking needs
to survive client IP changes, but for the MVP the (IP, port) tuple is
the client identity.

#### Allocate Response (header + 8 + N*8 bytes)

```
Offset  Size  Field
4       1     Assigned station
5       1     Assigned network
6       2     Reserved (0x0000)
8       4     Lease duration in seconds
12      2     Peer count (N)
14      N*8   Peer entries
```

Each peer entry (8 bytes):

```
Offset  Size  Field
0       1     Network
1       1     Station
2       2     AUN port
4       4     IPv4 address (network byte order)
```

The peer list bootstraps the client's AUN peer table. It includes all
stations the server knows about: static reservations, other DSCP-assigned
stations, and bridge endpoints.

#### Release Request (header + 2 bytes = 6 bytes total)

```
Offset  Size  Field
4       1     Station to release
5       1     Network
```

#### Renew Request (header + 6 bytes = 10 bytes total)

```
Offset  Size  Field
4       1     Station
5       1     Network
6       4     Requested lease duration in seconds (0 = same as before)
```

#### Renew Response (header + 4 bytes = 8 bytes total)

```
Offset  Size  Field
4       4     New lease duration in seconds
```

### Why Not JSON / Protobuf / TLVs?

- **JSON** requires a parser. Even a minimal one is more code than reading
  fixed offsets. Adds nothing for packets this small.
- **Protobuf** requires a code generator and runtime library. The whole
  point of using UDP instead of gRPC is to avoid this dependency.
- **TLVs** add extensibility but at the cost of parsing complexity. The
  protocol is versioned (header byte 1); if a future version needs new
  fields, it can define a new layout under a new version number. For a
  protocol with 4 operations and < 50 bytes per message, TLVs are
  over-engineering.

The Allocate response is the largest message. With 20 peers (a large
network), it's 4 + 8 + 2 + 160 = 174 bytes. Well within a single UDP
datagram.

### Lease Model

- Allocations have a lease duration (default: 1 hour, configurable)
- Clients must renew periodically (e.g., every 30 minutes)
- Expired leases are reclaimed and the station number returns to the pool
- Graceful shutdown calls `ReleaseStation` for immediate reclamation
- The server can notify other clients when the peer table changes
  (via a streaming RPC or mDNS TXT record updates)

### Peer Table Bootstrap

A key benefit of DSCP over manual configuration: the allocation response
includes a list of known peers (other DSCP-assigned stations, static
reservations, bridge endpoints). The client can call `AunBackend::add_peer()`
for each, eliminating the need for `--aun-map` arguments or preset peer lists.

As stations come and go, the DSCP server can push peer table updates via:

- A `SubscribePeerChanges` streaming RPC
- mDNS TXT record updates (Beebium already monitors these for service
  discovery)
- Periodic re-query by the client

## Interaction With PiEconetBridge

If a PiEconetBridge is present on the network, the DSCP server needs to:

1. **Know which wire stations exist** — either by querying the bridge
   (passive observation of its station bitmap) or by performing Machine Type
   sweeps through the bridge.

2. **Register DSCP-assigned stations with the bridge** — so the bridge can
   route traffic to them. PiEconetBridge's `DYNAMIC` network feature does
   this reactively (on first traffic). A DSCP server could pre-register
   stations by:
   - Adding them to the bridge's config and signalling a reload
   - Using PiEconetBridge's AUN exposure mechanism (the bridge learns about
     AUN hosts when they send traffic; the DSCP server could send a
     lightweight probe on behalf of newly-assigned stations)
   - Running as a PIPESERVER client with administrative privileges

3. **Exclude wire-side station numbers from the allocation pool** — the
   station bitmap from the bridge is the authority on which numbers are in
   use on the physical Econet. These are hard reservations that DSCP must
   never assign.

## Interaction With mDNS

Beebium already advertises gRPC services via mDNS. The econet-integration plan
(Phase 3) proposed Econet TXT records (`econet_station=N`); as built, the
station is published instead in the AUN transport's own `_aun._udp`
announcement (`net=`, `station=`, `port=`; see
[`aun-mdns-peer-discovery.md`](aun-mdns-peer-discovery.md)). With DSCP:

- The DSCP server advertises itself as `_beebium-dscp._tcp`
- Beebium instances discover the DSCP server via mDNS browse
- After allocation, each instance advertises its station number in its own
  mDNS TXT record
- The DSCP server can monitor mDNS advertisements as a secondary source of
  truth for which stations are active (belt-and-braces alongside the lease
  table)

## Speculative: Wire-Side DSCP for Real Hardware

This section is speculative. It explores whether DSCP could extend beyond
emulators to serve real BBC Micro hardware on a physical Econet.

### Precedent: *SETSTATION

On the BBC Master, the station number is stored in CMOS RAM (byte 0x0E of the
MC146818 RTC). Acorn provided no facility in MOS, NFS, or ANFS to write this
byte directly. Instead, a utility program `*SETSTATION` is stored in the
fileserver's Library directory. The workflow Acorn envisaged:

1. Network manager decides the number for a new machine
2. New Master is connected to the Econet with whatever is in CMOS (factory
   default or leftover from previous use)
3. User boots, the ANFS ROM reads the current CMOS value as the station number
4. User logs into the fileserver at the well-known station 254
5. User runs `*SETSTATION N` (loaded from the fileserver's Library)
6. The utility writes byte 0x0E in CMOS via the VIA
7. On next Ctrl-Break or power cycle, ANFS reads the new number

This works with a precondition: the current station number (whatever is in
CMOS) must not collide with another active station. On real Econet, a
collision means two stations both respond to the same scout frame, causing
electrical bus contention.

### The Key Observation

A BBC Micro can initiate communication with a well-known station number
before its own number is "correct" — it just needs to be unique at that
moment. The source address in outgoing frames is whatever the station ID
register returns. The destination station doesn't care what the source number
*means*, only that it can address the reply.

This means a DSCP service could be an Econet station at a conventional number.
The service is essentially `*AUTOSETSTATION`: instead of the user telling the
machine its number, the machine asks the network for one.

### How It Would Work

The DSCP server would be an Econet station at a well-known address (e.g.,
station 253, or a user-configurable number). It could be implemented as:

- A PiEconetBridge PIPESERVER in PASSTHRU mode (the DSCP server genuinely
  *is* an Econet station — it responds to Econet frames — it just happens
  to be implemented as an external process connected via named pipe)
- A Beebium instance with a DSCP service ROM
- A custom program on a real BBC Micro or Raspberry Pi

A client (real Master, or emulator over AUN) would:

1. Boot with its current station number (possibly stale or temporary)
2. Send an Econet frame to the DSCP server — either:
   - An Immediate operation (port 0, with a DSCP-specific control byte) for
     a lightweight single-frame exchange, or
   - A data frame to a designated DSCP port number for richer payloads
3. Receive a response containing the assigned station number
4. On the Master: write it to CMOS RAM (as `*SETSTATION` does) and reset
5. On an emulator: reconfigure the station ID register directly

On the Master, step 4 could be a small utility program — `*AUTOSETSTATION` —
stored in the fileserver's Library alongside `*SETSTATION`. It would:

- Send an Immediate or data frame to the DSCP server station
- Parse the assigned number from the response
- Write it to CMOS byte 0x0E via the VIA (same mechanism as `*SETSTATION`)
- Optionally trigger a soft reset (`*FX 200,2` then `Ctrl-Break`)

The program would be a few dozen bytes of 6502. The DSCP server could even
serve it as a file — a new Master boots, the user runs `*AUTOSETSTATION` from
the Library, and the machine configures itself.

### Constraints and Caveats

- **Model B cannot participate.** Station number is set by hardware links
  (DIP switches on S11), not software-writable. But Model Bs are the stations
  most likely to have fixed, well-known numbers — they're the "static
  reservations" in the DSCP server's allocation table.

- **Bootstrap collision risk.** If the client's current (stale) number
  collides with an active station, the DSCP request may fail due to bus
  contention. The DSCP server could mitigate this: if it knows the full
  station map, it can detect that the source address of a DSCP request
  belongs to a different machine (e.g., a Model B with fixed links) and
  respond with an allocation anyway — the reply might not reach the client
  if the collision is severe, but the client can retry.

- **The DSCP server needs a reserved station number.** This must be
  documented and conventional (like 254 for fileservers). Station 253 is
  a natural candidate (one below the fileserver), but this is arbitrary.
  The number could also be configurable, with a default.

- **Lease renewal is not natural over Econet.** Real hardware doesn't have
  background processes that send periodic keepalives. The lease model works
  for emulators (which can renew via gRPC) but not for real Masters. For
  wire-side clients, the allocation would need to be permanent (or at least
  very long-lived), with the DSCP server treating wire-side assignments as
  static once confirmed. Passive traffic observation (the bridge sees the
  station is still active) could substitute for explicit renewal.

- **This is speculative.** No real hardware testing has been done. The
  concept is sound in principle (it's just `*SETSTATION` with automatic
  number selection), but the implementation depends on PiEconetBridge
  PIPESERVER reliability, the 6502 utility program, and the assumption
  that the bootstrap collision window is manageable in practice.

## Emulator Landscape and Adoption

### Econet/AUN Support Survey (April 2026)

#### BBC Micro Emulators

| Emulator | Econet/AUN | Status | Open Source |
|----------|-----------|--------|-------------|
| **Beebium** | AUN over UDP, cycle-accurate ADLC | Working, tested with BeebEm fileserver | Yes (GPL) |
| **BeebEm** | AUN over UDP, poll-based ADLC | Working, tested with PiEconetBridge | Yes (GPL) |
| **B-Em** | AUN over UDP (BeebEm-derived code) | Working on `sf/econet` branch, tested with `aund` | Yes (GPL) |
| **jsbeeb** | Local-only (BroadcastChannel API) | In-browser tabs only; WebSocket AUN in development | Yes (MIT) |

#### Archimedes / RISC OS Emulators

| Emulator | Econet/AUN | Status | Open Source |
|----------|-----------|--------|-------------|
| **Arculator** | ADLC prototyped (2018, stalled); Ethernet podule emulation (v2.2) could theoretically carry AUN | Unconfirmed for AUN | Yes (GPL-2.0) |
| **RPCEmu** | No ADLC; bridged Ethernet could theoretically carry AUN via RISC OS AUN module | Unconfirmed | Yes (GPL-2.0) |
| **ArcEm** | Explicitly "not emulated (or broken!)" | Not implemented | Yes (GPL) |
| **Virtual Acorn** | Vendor confirmed AUN doesn't work | Not supported | No (proprietary) |

No Archimedes/RISC OS emulator has confirmed, working Econet or AUN
networking. Arculator comes closest: its Ethernet podule emulation (AEH50/
AEH54 in PCAP/bridged mode) could carry AUN traffic since AUN is just UDP
on port 32768, but nobody has reported success. RPCEmu's bridged Ethernet
mode could also theoretically carry AUN (RISC OS 5's AUN module is
confirmed working on IOMD, which RPCEmu emulates), but again unconfirmed.

#### Other Participants

| System | Econet/AUN | Notes |
|--------|-----------|-------|
| **PiEconetBridge** | Wire Econet + AUN gateway | The central interop hub for mixed networks |
| **aund** | AUN fileserver (Unix) | Lightweight Level 3 fileserver, AUN-native |
| **Real RISC OS on Ethernet** | AUN via RISC OS AUN module | Standard AUN, well-established |
| **Real BBC Micro hardware** | Wire Econet only | Fixed station numbers, no AUN awareness |

### Immediate DSCP Audience

The practical audience for DSCP adoption is:

1. **Beebium** — first implementation, reference client and server
2. **BeebEm** — the largest user base, currently suffering from their
   proprietary 0xFF discovery and self-assignment mechanism
3. **B-Em** — shares BeebEm's Econet code, would benefit from the same fix
4. **PiEconetBridge** — as a DSCP-aware bridge (wire-side station awareness)

Archimedes/RISC OS emulators are a future audience if/when they get AUN
working. The lightweight UDP protocol has no barriers to adoption on their
side.

### Adoption Strategy

The key insight is that BeebEm's developers have already identified the
problem (station number collisions, discovery) and built a solution that
doesn't work well (proprietary 0xFF packets, destructive collision
resolution, non-standard port formula, no interop with real hardware).
DSCP should be positioned as a solution to *their* problem, not just ours.

**Design for adoption:**

- **Zero exotic dependencies.** The UDP protocol requires nothing beyond
  BSD sockets. No gRPC, no protobuf, no JSON parser. Any emulator can
  implement a DSCP client in a single source file. This is a deliberate
  contrast to Beebium's gRPC-based internal APIs, which are right for
  Beebium's architecture but wrong for a cross-emulator standard.

- **Publish a standalone specification.** The protocol should be documented
  independently of Beebium's codebase — a short document (protocol format,
  mDNS service type, behaviour rules) that any developer can implement
  from. Not a Beebium feature doc, but a community protocol spec.

- **Provide a reference server that anyone can run.** `beebium-dscp` should
  be buildable without the rest of the Beebium tree. Ideally it could be
  a single C file with no dependencies beyond POSIX sockets and an mDNS
  library (or even a compile-time option to disable mDNS and use only
  explicit addressing). If a BeebEm developer can clone one repo, type
  `make`, and have a working DSCP server, adoption becomes realistic.

- **Provide a reference client library.** A small C or C++ source file
  (header + implementation, no templates, no C++20) that any emulator can
  drop into its build: `dscp_allocate()`, `dscp_release()`,
  `dscp_renew()`. Again, single file, BSD sockets only.

- **Backwards compatibility with static config.** DSCP is always optional.
  `--station 42` continues to work. Emulators that don't implement DSCP
  can still participate in the network as statically-configured peers —
  the DSCP server's allocation table has a reservation mechanism for them.
  This means adoption can be incremental: one emulator adds DSCP, others
  follow when convenient.

- **Engage the community early.** The Stardot forum (stardot.org.uk) is
  where the BBC Micro and Archimedes retrocomputing community congregates.
  The PiEconetBridge developer (cr12925) is active there. Posting a
  protocol draft for comment — before implementation is complete — would
  surface design issues and build buy-in. The BeebEm developers might
  adopt DSCP if they see it as a community standard rather than a Beebium
  feature.

- **Help BeebEm migrate.** The kindest thing we can do for the BeebEm
  developers is offer them a path away from their 0xFF mechanism that
  doesn't require rearchitecting their Econet code. A DSCP client is a
  few dozen lines of socket code called once at startup, before the ADLC
  is initialised. It doesn't touch the four-way handshake, the FIFO
  management, or the status register logic. It replaces
  `AllocateNewAddress()` with a UDP round-trip. If we provide the
  reference client library, the integration effort is minimal.

- **Name it neutrally.** "DSCP" is fine as a working title, but the
  published protocol should not have "Beebium" in its name. Something
  like "Econet Station Assignment Protocol" (ESAP) or simply "DSCP"
  (Dynamic Station Configuration Protocol) — a name that implies it
  belongs to the Econet ecosystem, not to one emulator.

- **Submit PRs to other emulators directly.** Rather than publishing a
  spec and hoping others implement it, we can write the integration code
  ourselves and submit pull requests to BeebEm, B-Em, and eventually
  Arculator/RPCEmu. A PR with working code, a clear commit message, and
  passing tests is far more persuasive than a spec document. The
  integration is small (a single-file DSCP client, called once at startup)
  and self-contained — it doesn't require rearchitecting the host
  emulator's Econet code.

- **Test against real hardware.** We have access to real BBC Micro Econet
  hardware and a PiEconetBridge. This means we can validate the full
  chain: DSCP server assigns a number to an emulator, the emulator
  communicates over AUN to the bridge, the bridge routes to a real BBC
  Micro on physical Econet. We can verify there are no identity mismatches,
  no routing failures, no collisions with hardware stations. This level of
  end-to-end testing is rare in the emulator community and lends
  credibility to the protocol. Test results (including logs, packet
  captures, and screenshots of `*STATIONS` output showing emulated and
  real machines coexisting) should accompany any PR or spec publication.

## Open Questions

1. **Should DSCP be mandatory or optional?** Probably optional: `--station N`
   continues to work for static assignment. `--station auto` (or equivalent
   preset/gRPC config) triggers DSCP. If no DSCP server is found, fall back
   to an error rather than silent self-assignment.

2. **What happens if the DSCP server goes down?** Existing leases continue
   until expiry. New allocations fail. Clients should cache their last
   assignment and attempt to re-acquire the same number on reconnection
   (via `preferred_station`).

3. **Multiple DSCP servers?** Probably not needed initially. If required,
   a simple leader-election (or partition by network number) could work.

4. **Should the DSCP server also be an AUN peer?** It doesn't need to
   participate in Econet traffic. It's purely administrative. But it could
   optionally act as a bridge endpoint, forwarding traffic between
   DSCP-assigned stations and wire-side stations via PiEconetBridge.

5. **Security?** On a trusted LAN, none needed. For wider deployments,
   a shared secret or token in the DSCP header could prevent spoofing.
   Not a priority for the MVP.

6. **What is the minimum viable implementation?** A standalone UDP server
   that maintains an in-memory allocation table, responds to Allocate/
   Release/Renew, and is discoverable via mDNS. No wire-side enumeration,
   no bridge integration, no persistence. This is enough for multiple
   emulator instances on a LAN to avoid station number collisions. The
   server should be buildable as a single C file with no dependencies
   beyond POSIX sockets and optionally an mDNS library.

7. **Should the protocol name avoid "Beebium"?** Yes. If this is to be a
   community protocol, it should have a neutral name. "DSCP" (Dynamic
   Station Configuration Protocol) works, or perhaps "Econet Station
   Assignment Protocol" (ESAP). The name should imply Econet ecosystem
   ownership, not one emulator's branding.
