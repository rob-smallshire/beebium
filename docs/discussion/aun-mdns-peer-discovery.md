# AUN Peer Discovery via mDNS / DNS-SD

Replacing the manual `--aun map=` peer table with opportunistic discovery of other AUN endpoints on the local network, using the standard mDNS / DNS-SD machinery Beebium already employs for service discovery.

**Status (October 2026): implemented and merged.** Beebium publishes and browses `_aun._udp` on all three platforms -- macOS (Bonjour), Windows (Apple Bonjour when installed, else native DnsService*), and Linux (Avahi advertise + browse). See [`docs/networking.md`](../networking.md) for the user-facing description. What differs from the proposal below:

* Net numbers are 0..255 everywhere (`--aun net=`, the `net=` TXT field, `map=`, `subnet=`), not 0..127 (#139).
* Discovered peers are one layer of the transport's `AunPeerSet`, resolved by provenance precedence Api > Launch > MapFile > Discovered > Subnet. A shadowed discovered peer is still recorded and reappears if the operator entry is removed (#54/#55); a per-user map file (`aun-map.json`) adds the MapFile and Subnet layers (#139, see [`aun-peer-map-file.md`](aun-peer-map-file.md)).
* Eviction is not TTL-based: a remote peer goes when its DNS-SD advertisement is withdrawn; a same-host peer goes when a ~2.5 s bind-probe liveness sweep finds its port free.
* Two stations claiming one `(net, station)`: first live station wins, the refused one is parked and adopted when the number frees, and a collision report shows the collisions in effect (#68, #138); `since=` gives incumbent/newcomer wording (#147); a renumbered station rebuilds from remembered announcements (#148).
* `--aun discovery=off|announce|browse|on` (default `on`) chooses whether to publish and/or browse (#158); `--station auto` uses discovery to pick a free number (#67, #160, #161).
* DNS-SD refs are torn down by joining the event thread before deallocating, and the advertiser is `select()`-driven like the browser (#155).
---

## Problem

Today every Beebium instance that wants to talk to AUN peers must enumerate them by hand: `--aun map=0.254@127.0.0.1@32768:map=0.253@127.0.0.1@32769:...`. Three machines on a LAN need three sets of `map=` entries that all stay consistent. Adding a fourth machine means editing four configurations. There is no way for an instance to learn about peers that come and go.

This isn't a fundamental AUN problem — AUN's `(net, stn) → (ip, port)` table is per-instance state, but nothing about the protocol prevents that table being populated by discovery rather than by configuration. The `--aun map=` mechanism is just the only mechanism Beebium currently offers.

mDNS / DNS-SD solves this exact category of problem on local networks: each AUN endpoint advertises itself, listeners populate their tables from the announcements, peers come and go without manual intervention.

## Why mDNS, why now

* Beebium already uses mDNS-family service discovery (`src/discovery/`) — Bonjour on macOS, Avahi on Linux, Windows DNS-SD APIs on Win32. Reuse rather than reinvent.
* The infrastructure Stage 6 of the Extension UI Framework deliberately did *not* build (a manual Add/Remove peer form on the AUN panel) was held back specifically for an auto-discovery direction; this is that direction.
* DSCP (Dynamic Station Configuration Protocol — see [`dynamic-station-config-protocol.md`](dynamic-station-config-protocol.md)) is the bigger sibling that *also* solves the manual coordination problem, but for station-number assignment rather than peer-table population. The two are independent: mDNS solves peer discovery without solving station assignment, and vice versa. mDNS is the smaller of the two and ships immediate value; DSCP can layer on top later.

## Design decisions

### Service type: `_aun._udp`

Vendor-neutral DNS-SD service type. Other AUN-speaking implementations (BeebEm, future-Beebium, PiEconetBridge on its AUN side, real Acorn hardware via an AUN bridge) can advertise on the same name without coordination.

Rejected alternatives:

* `_beebium-*` — would partition the announcement space along implementation lines for no protocol-level reason. AUN is the protocol; Beebium is one implementation.
* `_acorn-universal_networking-station._udp` (literal) — exceeds the 15-character DNS-SD service-name limit (33 chars), contains an underscore mid-name (non-standard per RFC 6763), conflates protocol with instance-role.
* `_aun-econet._udp` / `_acorn-econet._udp` — more specific, harder to collide with anything else, but signals the family of protocols rather than the specific protocol that's actually being advertised. The announcement carries an AUN endpoint; "AUN" is the right name for it.

The instance name (the part before the service type, e.g. `Beebium-32._aun._udp.local.`) is per-host and we control it. Other implementations would use their own instance names alongside ours under the shared service type.

### TXT record schema

Mandatory:

* `version=1` — schema version of the announcement; bump on incompatible changes. Consumers should ignore announcements with a `version` they don't understand rather than guessing.
* `station=N` — Econet station number (1-254). Mandatory because there is no useful default.
* `port=N` — UDP port the AUN endpoint is bound to. Mandatory.
* `net=N` — Econet net number (0-255). Mandatory because the protocol allows non-zero nets and consumers must know which net to address us as. **See "Net number semantics" below for why this can't sensibly default-by-omission.**

Optional:

* `impl=<name>` — implementation identifier, e.g. `beebium`, `beebem`, `pi-econet-bridge`. **Diagnostic only.** Never used for behavioural decisions; never used to filter or prefer announcements. We refuse to grow a fragmented protocol where each implementation only talks to its own kind.
* `impl-version=<version>` — implementation version. Diagnostic only.
* `impl-identity=<opaque>` — per-instance identifier. The *key* is vendor-neutral; the *value's* format is scoped by `impl=`, so each implementation picks what makes sense for it. Beebium populates this with the same machine UUID it publishes as `uuid` on its `_beebium._tcp` gRPC announcement, so a discovering tool can correlate the AUN peer with the matching gRPC server. Another implementation might use a hostname, a hash of its config, or omit the field. Consumers must treat the value as opaque (don't parse it; don't infer anything from its format) and must not use it for behavioural decisions — diagnostic and correlation only.
* `since=<unix seconds>` — when this station bound its socket, i.e. first claimed this number. One behavioural use, and a narrow one: when two stations claim the same `(net, station)`, each side compares the other's `since` with its own to decide which bound first. The earlier `since` is the incumbent; a tie (same second) breaks on the `impl-identity` strings so both sides agree; a peer that omits `since` counts as earlier (the incumbent). This only chooses *which warning each operator sees* — the routing rule is still first-live-wins regardless — so an implementation that omits it loses nothing but the asymmetric wording. A station change refreshes it, because changing number is a fresh claim. See issue #147.

Format: standard DNS-SD TXT record key=value pairs, each ≤ 255 bytes (well within limits for these short values).

### Net number semantics

This is the subtle bit. The conversation that produced this doc spent considerable time on it because the answer isn't obvious.

**Net 0 is local-relative, not global.** On a real Econet wire, every station and bridge interprets `dest_net=0` as "this segment, don't route" and `src_net=0` as "I came from this segment". It's not a globally-unique identifier; it means "here" relative to the receiver's view. On a multi-segment Econet, bridges define the actual net numbers — a station doesn't intrinsically know its own net, it asks the local bridge.

**AUN does not enforce a flat-net assumption.** The protocol's per-station `(net, stn) → (ip, port)` peer table is the *de facto* routing table; nothing requires every AUN endpoint to claim the same net. You can build a bridgeless AUN deployment where:

* Station A claims `net=3`, station 10
* Station B claims `net=5`, station 42
* Station C claims `net=0`, station 254

…and as long as their peer tables agree (A's table has B at net 5, B's table has A at net 3, etc.), routing works. There is no central authority. The "flat AUN cloud, everyone on net 0" model is just one common topology, not a protocol requirement.

**Implication for the announcement:** `net=N` must be a per-station self-declaration that consumers populate into their peer table verbatim. It cannot be omitted-with-default-0 because that would silently force every announcement into the flat-cloud model, breaking deployments that legitimately use multi-net topologies. Make it mandatory.

**For Beebium specifically:** when this was written `AunBackend::local_net` was hardcoded to 0. It is now configurable with `--aun net=N` (a preset's `econet.transport.parameters.net`) and announced verbatim — see "Implementation sequence" below.

### Operator-configured peers always override discovered peers

`--aun map=` (and equivalent preset entries) are operator intent and authoritative. mDNS-discovered peers are advisory and only apply when the operator hasn't said otherwise. This means:

* If `--aun map=0.42@10.0.0.1@32768` is configured AND an announcement arrives claiming `(net=0, stn=42)` at a different `(ip, port)`, the configured entry wins. The discovered entry is recorded but shadowed, so it takes over if the configured entry is later removed.
* Discovered peers populate the table for `(net, stn)` pairs that the operator has not configured.
* Removing a `--aun map=` entry doesn't remove the corresponding discovered entry if one exists; the discovered entry takes over.

This rule is non-negotiable: discovery is a convenience layer; the operator's explicit config is the source of truth.

### Stale-peer eviction

Discovered peers are removed from the table when their announcement is no longer being received. Standard mDNS practice: each announcement carries a TTL; we re-evaluate at TTL/2 boundaries; missing-renewal removes. Operator-configured peers are immune to eviction.

### What the announcement does NOT carry

* `aun-protocol-version` — we don't have a versioned AUN protocol on the wire today; the announcement schema's `version=1` is sufficient for evolving the announcement format itself.
* IP address of the announcer — already available from the DNS-SD A/AAAA record that accompanies the SRV record. Don't duplicate it in the TXT record.
* List of peers the announcer knows about — peer-of-peer transitive discovery is overkill for a LAN; mDNS already sees every announcer.
* Bridge information — bridges are a separate concept (an AUN-to-real-Econet bridge would announce itself as a bridge service, probably under a different DNS-SD service type, e.g. `_acorn-bridge._udp`). Out of scope here.

## Findings worth recording

### What other emulators do (BeebEm)

[BeebEm-Windows](https://github.com/stardot/beebem-windows) (the most actively maintained BeebEm fork) handles AUN net numbers via a **static IP-subnet → Econet-net mapping** in `AUNMap`. Format: `AddMap <ip-prefix> <net-number>`, e.g. `AddMap 192.168.0.0 128` declares "any host in 192.168.0.x is on Econet net 128". At startup BeebEm:

* Looks at the local machine's IP.
* Finds the matching `AUNMap` entry.
* Sets `EconetNetworkID` to that net number.
* Sets `EconetStationID` from the last octet of the IP (192.168.0.42 → station 42).

Combined with `STRICT` mode (infer addresses for unknown peers from the same map) and `LEARN` mode (add receivers dynamically from incoming traffic), this provides a "just put the right `AUNMap` on every host and it works" experience — but the topology is still hand-maintained per host, just at the IP-mapping layer rather than the peer-mapping layer.

**No bridge-discovery protocol.** BeebEm has no runtime negotiation with anyone about its net assignment. (There IS a real Acorn Econet bridge protocol used by physical bridges between segments, and PiEconetBridge implements it on its real-Econet side, but BeebEm doesn't speak it for self-discovery.)

**No mDNS support today.** BeebEm doesn't announce or listen on mDNS.

### What PiEconetBridge does

[PiEconetBridge](https://github.com/cr12925/PiEconetBridge) is the most actively-developed AUN/Econet bridging implementation and has explored a lot of the relevant design space. It is the closest existing prior art to what we're building.

**Real Acorn bridge protocol on the Econet wire.** PiEconetBridge speaks the Acorn bridge protocol on Econet port `0x9C`:

| Ctrl | Name | Purpose |
|------|------|---------|
| `0x80` | `BRIDGE_RESET` | Bridge broadcasts: "I'm here, these are the nets I reach" |
| `0x81` | `BRIDGE_UPDATE` | Incremental net-list change |
| `0x82` | `BRIDGE_WHATNET` | Station asks: "what net am I on?" |
| `0x83` | `BRIDGE_ISNET` | Bridge responds yes/no for a specific net |

This is the bridge-to-station discovery layer that lives on the Econet wire underneath AUN. It's separate from anything mDNS does and doesn't currently affect Beebium (Beebium has no real wire). Once Beebium grows wire-side participation via Piconet *and* a real bridge is on that same wire, the emulated BBC OS would issue `BRIDGE_WHATNET` and the bridge would answer with the wire's net number — that's where the bridge-supplied "what net am I on?" answer would come from for a Piconet-equipped Beebium.

**TRUNK protocol for bridge-to-bridge over UDP.** PiEconetBridge has a dedicated UDP-encapsulated bridge interconnect: `TRUNK ON PORT 9000 TO myfriend.econet.org:9500 KEY abcdef123456`. HMAC-keyed, optionally one-end-dynamic-IP, with per-net XLATE for renumbering and per-net BRIDGE DROP filtering. Not relevant to mDNS endpoint discovery — different layer (bridge interconnect rather than endpoint discovery).

**EXPOSE for AUN-side endpoints.** `EXPOSE NET 1 ON 172.17.1.0 PORT FIXED AUTO` maps every Econet station on net 1 to a per-station AUN endpoint at `172.17.1.<stn>:32768`. Each EXPOSEd station has *its own IP+port*, unlike Beebium where one process serves one station at one endpoint. This works fine with our mDNS schema — a PiEconetBridge would emit one announcement per EXPOSEd station, each with its own SRV record pointing at that station's IP. Per-station instance names disambiguate.

**`DYNAMIC <net>` for reactive AUN learning.** Reserves a net number for AUN/IP stations the bridge doesn't know about upfront. When unknown traffic arrives, the bridge allocates a spare station from the dynamic net (with an inactivity timeout). This is essentially ARP-cache-for-AUN — *reactive* rather than proactive (peer must transmit before being learned), and *per-bridge* (no propagation to other peers). It is the closest existing equivalent to what we want, but mDNS supersedes it cleanly: announcements are proactive (peers known before first traffic), symmetric (every peer auto-populates from the same announcement stream), and they carry the peer's self-declared `(net, stn)` so the bridge doesn't have to allocate from a dynamic pool.

**No mDNS support today.** PiEconetBridge currently uses static `AUN MAP HOST` entries (similar to BeebEm's `AddHost`) for known peers and `DYNAMIC` for the rest. Adopting our `_aun._udp` schema would let it: (a) announce its EXPOSEd stations so other AUN peers discover them automatically; (b) consume announcements from peers like Beebium and treat them as if they were `AUN MAP HOST` entries, with `DYNAMIC` becoming a fallback for non-mDNS-speaking peers.

**Implication for our design:** the schema is adoptable by PiEconetBridge as-is, with one open question — how (if at all) bridges should signal "I'm a bridge announcing on behalf of an exposed station" versus "I'm a direct AUN endpoint". Two options:

1. **A `via=bridge|direct` TXT field** (defaulting to `direct` if absent). Diagnostic-only — addressing-equivalent endpoints don't change consumer behaviour, but the field makes the topology visible.
2. **A separate `_acorn-bridge._udp` service type** for bridges-as-bridges, with bridges advertising the list of nets they route. Individual exposed stations *also* announce under `_aun._udp` as ordinary endpoints. This preserves the "an AUN announcement is just an endpoint" model.

Option 2 is the preferred direction — cleaner separation between "endpoint discovery" and "topology discovery". It mirrors the existing Acorn distinction between station addresses and bridge advertisements. **Not implementing it now** because: (a) Beebium doesn't act as a bridge, so the announcement-as-bridge case has no Beebium-side consumer or producer yet; (b) PiEconetBridge could add a separate `_acorn-bridge._udp` advertiser later without affecting `_aun._udp` consumers (they remain backwards-compatible); (c) the bridge-as-topology-source feature interacts with DSCP, trunking, and `XLATE` net-translation in ways that benefit from being designed alongside those rather than in isolation.

For now: bridges adopting our schema announce their EXPOSEd stations via `_aun._udp` and that's it. The `_acorn-bridge._udp` service type stays reserved as a future extension point.

### Why we're not adopting BeebEm's IP-subnet-derived approach

Beebium could in principle add an `AUNMap`-equivalent feature to derive `local_net` from the host's IP. It would not interoperate with BeebEm at the file-format level (different config syntax), but it would replicate the conceptual behaviour. We're not doing this because:

* It assumes IP topology mirrors Acorn-net topology, which is an organisational accident the user has to engineer. Multi-net AUN on a single subnet, or a single AUN net spanning multiple IP subnets, both become awkward.
* mDNS's per-station-self-declaration model gives the operator direct control over net assignment without needing IP-subnet correspondence. Each instance just declares its net via `--aun net=N` and announces it.
* The IP-subnet model requires a static config file (`AUNMap`-equivalent) that has to stay in sync across hosts. We're trying to *eliminate* per-host static config, not introduce a new flavour of it.

We should still support per-instance `local_net` configuration via `--aun net=N` so Beebium can participate in non-flat AUN deployments. That's necessary regardless of whether discovery is mDNS or IP-subnet-derived.

### Interop with non-mDNS AUN implementations

mDNS is opportunistic. Beebium-to-BeebEm interop continues to work via static `AddHost` entries in BeebEm's `Econet.cfg` and `--aun map=` in Beebium, exactly as today. mDNS announcements are fire-and-forget — BeebEm won't see them, and that's fine. Cross-implementation deployments are explicit-configuration territory.

If/when other implementations grow mDNS support, they slot in automatically — `_aun._udp` is the shared name and the TXT record schema is implementation-neutral.

### Why mDNS is independently useful from DSCP

[DSCP](dynamic-station-config-protocol.md) addresses station-number assignment ("how does this instance get a unique station number?"). mDNS peer discovery addresses peer-table population ("how does this instance learn about other peers' (station, IP, port)?").

They share the goal of reducing manual AUN-deployment friction but solve orthogonal sub-problems. Either is independently useful:

* **mDNS only**: every operator manually picks each instance's station number via `--station N` (current model), but `--aun map=` becomes optional.
* **DSCP only**: instances auto-acquire station numbers but operators still manually specify peer maps.
* **Both**: zero per-instance AUN configuration in the common case; operators just launch Beebium and it joins the local AUN cloud.

Build mDNS first because:

* It's the smaller piece (no broker process, no peer-to-peer negotiation, no conflict resolution).
* It exercises the existing `src/discovery/` infrastructure rather than building a new protocol.
* DSCP becomes more attractive once mDNS is in place — the visible win of "I just plugged in another instance and it works" is bigger when peer discovery is also automatic.

## Implementation sequence

The work fits naturally into a single branch with four green-each-step commits. Each step is independently testable.

### Step 1: AunBackend::local_net configurable

**Pure AUN correctness work, independent of discovery.** Currently `AunBackend::local_net` is hardcoded to 0; the existing memory note (`AUN map must use net 0 ... AunBackend hardcodes local_net=0`) describes the symptom.

What changes:

* `AunBackend` constructor takes `local_net` as a parameter (default 0 to preserve current behaviour). Threaded through `AunEconetTransportExtension::create_backend()` from a new `--aun net=N` CLI parameter and matching `econet.transport.parameters.net` preset field.
* Validation: 0-127 (Econet's high bit is reserved for the routing protocol).
* Net translation at the AUN/BBC boundary:
  * Outbound from BBC: `dest_net=0` (BBC's "this segment" addressing) → look up `(local_net, dest_stn)` in the peer table. The BBC says "local segment"; we know "local segment" means our `local_net` from our perspective.
  * Inbound from peer: `src_net=local_net` → present to BBC as `src_net=0` (since that net IS our local segment from the BBC's view). `src_net=other` → present as `src_net=other` unchanged (genuinely remote).
* Tests: two AunBackend instances with different `local_net` values, manually configured peer maps, verify routing both directions.

### Step 2: mDNS announcement production

`AunBackend` (or a new `AunDiscoveryAnnouncer` collaborator) registers a `_aun._udp` advertisement at startup using the existing `src/discovery/` machinery, with the TXT record populated from `local_net`, the local station ID, and the bound UDP port. Re-publish on `local_net` or station-ID changes.

Tests: spin up an `AunBackend`, query the local Bonjour/Avahi resolver, confirm the announcement is visible with the expected TXT record contents. Cross-platform tests reuse existing discovery test infrastructure.

### Step 3: mDNS announcement consumption

`AunBackend` (or `AunDiscoverySubscriber`) subscribes to `_aun._udp` and populates the peer table from announcements:

* Skip our own announcement (compare with our local `(net, stn)`).
* Operator-configured entries take precedence — if `(net, stn)` is already in the peer table from `--aun map=`, log conflict and ignore the discovered entry.
* Otherwise add `(net, stn) → (ip, port)` to the peer table.
* Stale-peer eviction on announcement TTL expiry.
* Distinguish operator-configured entries from discovered entries internally so eviction only applies to the latter.

Tests: spin up two `AunBackend` instances on different ports, verify each appears in the other's peer table within a bounded time. Verify operator-configured override behaviour.

### Step 4: End-to-end test

Two Beebium instances on different declared nets (`--aun net=3` and `--aun net=5`), no `--aun map=` configuration, verify they discover each other via mDNS and route AUN traffic correctly. Exercises every layer: announcement production, announcement consumption, peer-table merging, net translation at the AUN/BBC boundary.

## Implementation notes (as built)

These are the non-obvious things the macOS implementation (`src/discovery/src/BonjourBrowser.cpp`) got wrong at least once; they are documented here so they aren't reintroduced. The browser drives all DNS-SD refs (browse + per-instance resolve + getaddrinfo) from a single background thread with `select()`.

### A browse delivers each service once *per network interface*

`DNSServiceBrowse` fires the Add callback once for every interface the service is visible on — on a machine with VPN/utun links that's several callbacks for a single peer. Two consequences:

* **Resolve each instance only once.** Starting a fresh `DNSServiceResolve` for each duplicate Add would deallocate the previous, still-in-flight ref while the event loop holds it in its current `select()` snapshot, then call `DNSServiceProcessResult` on freed memory — a use-after-free that intermittently corrupted the whole DNS-SD pipeline so no peer resolved. `begin_resolve` therefore ignores an Add for an instance that already has a resolve/getaddrinfo in flight or is already announced.
* **Re-validate refs after servicing the browse ref.** Processing the browse ref can start new resolves or, on a Remove, free resolve/addr refs that are still in the pre-`select` snapshot. The loop re-collects the set of still-live refs after servicing browse and only services those.

### Resolve the address on `kDNSServiceInterfaceIndexAny`, not the arrival interface

Because we resolve each instance only once (above), we hold exactly one interface index — whichever Add arrived first. Passing that index to `DNSServiceGetAddrInfo` is wrong: the first interface is often one whose A record never answers (a utun/VPN link), so the address query never completes, its callback never fires, and the peer is silently dropped. This specifically bit the *second* station launched, which sees the first as an already-present service and resolves it on whatever interface happened to arrive first; the first-launched station was unaffected because its two address resolutions (its own announcement, then the peer arriving live) happen sequentially. Use `kDNSServiceInterfaceIndexAny` so the daemon answers from whichever interface actually holds the record — one reachable address is all a UDP peer needs.

### Testing limitation

The end-to-end coverage (`tests/test_aun_mdns_e2e.cpp`, tag `[.mdns]`) includes a regression test for the already-present-peer case above (start one station, wait, start a second, assert the second discovers the first — verified to fail when the interface fix is reverted). But these are real-mDNS tests **excluded from the default CTest run**, because mDNS is not dependable in CI (no reliable responder; stray records leak between runs). They must be run explicitly (`ctest`/the binary with `[.mdns]`) on a machine with a working responder — a developer pre-release check, not per-commit CI. There is no deterministic CI equivalent: `BonjourBrowser` calls `dns_sd.h` directly with no mockable seam, and a unit test built on a fake DNS-SD layer would only assert "we pass the flag" rather than exercise real discovery, which was judged not worth the abstraction.

## Out of scope

* **DSCP integration.** Tracked separately at [`dynamic-station-config-protocol.md`](dynamic-station-config-protocol.md). Layers naturally on top of mDNS but isn't a prerequisite.
* **`STRICT` / `LEARN` mode equivalents.** BeebEm-style address inference from incoming-packet IP is unnecessary once mDNS is in place; passive inference is also less reliable than explicit announcements. Skip.
* **IP-subnet-derived `local_net`.** See "Why we're not adopting BeebEm's approach" above. The operator sets `local_net` explicitly via `--aun net=N`.
* **Bridge announcements.** A future bridge-as-bridge announcement (a Beebium machine type acting as an AUN-to-real-Econet bridge, or PiEconetBridge growing mDNS support) would advertise itself under a different DNS-SD service type — `_acorn-bridge._udp` is the reserved name — and announce the list of nets it routes. The "What PiEconetBridge does" subsection above covers the rationale for keeping this separate from `_aun._udp`. Pure AUN endpoints (Beebium, BeebEm, individual EXPOSEd stations behind a bridge) don't need to know whether bridges exist; they just announce themselves as endpoints. Out of scope for this design; the `_aun._udp` schema does not preclude a future `_acorn-bridge._udp` companion.
* **Authentication / authorisation of announcements.** mDNS is unauthenticated by design and runs on the local LAN only; we trust the local network. If/when that changes, a separate "trusted peers" allowlist would be the response. Not now.
* **WAN AUN deployments.** mDNS doesn't traverse routers. Operators wanting AUN across WANs continue to use static `--aun map=` entries pointing at WAN-reachable IPs.

## References

* RFC 6763 — DNS-Based Service Discovery (DNS-SD)
* RFC 6335 — IANA service name registry rules; the 15-char service-name limit
* `docs/discussion/dynamic-station-config-protocol.md` — the station-assignment sibling problem
* `docs/networking.md` — current AUN behaviour, `--aun map=` syntax
* BeebEm-Windows: `Src/Econet.cpp` — reference implementation of the IP-subnet → Econet-net mapping approach (`AUNMap`, `MASSAGENETS`, `STRICT`, `LEARN`)
* PiEconetBridge: `utilities/econet-hpbridge.c` and `docs/README.CONFIG-v2.1` — reference implementation of the Acorn bridge protocol (`BRIDGE_PORT 0x9C`), the AUN-side TRUNK protocol, and the EXPOSE / DYNAMIC mechanisms for AUN endpoint mapping and reactive learning. Closest existing prior art for what we're building on the discovery side.
