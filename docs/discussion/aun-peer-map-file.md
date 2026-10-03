# A persistent AUN peer map file

**Status (October 2026): implemented as proposed** (#139 reader, MapFile and
Subnet provenances, subnet rules, `map-file=`/`subnet=`, reload; #141 the
`AunService` map RPCs and the seven CLI subcommands; #140 the converter
`tools/aun/convert_beebem_econet_cfg.py`; #142 and #144 the sidebar editing,
built on the EditableList and FileReference primitives, with the file shown
as "Shared AUN map"; #143 the bridge acceptance test; #146 poll-driven
reloads refresh the sidebar). Differences from the text below: the mtime
poll rides the discovery subscriber's sweep, so it runs only while the
instance browses (`--aun discovery=on` or `browse`); with `off` or
`announce` a hand edit is picked up by `ReloadMap`, an edit RPC, or a
relaunch. An entry for this machine's own station is not filtered out (it
is applied like any other peer, and `--station auto` counts it as
occupied). The bridge acceptance test still maps the bridge with `--aun
map=`; the map-file form of the recipe is in
[`networking.md`](../networking.md#connecting-to-a-pieconetbridge). The
user-facing description is [`networking.md`](../networking.md#the-aun-map-file-aun-mapjson).

Originally drafted for discussion 2026-10-01. Prompted by mph1708's Stardot
question (https://stardot.org.uk/forums/viewtopic.php?p=493685#p493685):
how to link the Station 80 AUN preset to a PiEconetBridge that already
serves a wired Econet, a BeebEm laptop and a RISC OS Pi 400 over AUN.
Related: #55 (peer-table RPCs need an active backend), #54 (enable does not
bind), #67 (pick a free station from mDNS), #66 (multi-homed case 2).

## 1. Where the peer map came from (before #55)

The AUN routing table (`AunBackend`'s peer table) is populated from three
places, and every entry carries one of two provenances:

| Source | Scope | Provenance (`PeerSource`) | Sidebar shows |
|--------|-------|---------------------------|---------------|
| `--aun map=net.stn@ip@port` on the command line, or the same `map` key in a preset's `econet.transport.parameters` | this launch | `OperatorConfigured` | nothing |
| `AunService.AddPeer` over ExtensionRpc (Python `bbc.transport[Aun].add_peer`, TypeScript `Aun.addPeer`) | this process, until removed | `OperatorConfigured` | nothing |
| `_aun._udp` mDNS announcements from other Beebium instances | while advertised (same-host peers: while their port is live) | `Discovered` | `mDNS` |

Operator entries always win over discovered ones. There is no persistent
map, no way to share entries between instances on the same host, no
hostname support (IPv4 literals only), and no discovery opt-out.

So for any peer that does not advertise itself, which is every real
machine (RISC OS, a PiEconetBridge, BeebEm), the only routes are a
`map=` on every launch, a preset per peer set, or a script that calls
`AddPeer` after the backend is up. None of these is a good answer for
"my Archimedes is at 192.168.1.40, always".

## 2. Proposal

A per-user file holding the static part of the AUN world: the machines that
do not announce themselves. It is a fourth provenance, read by every
Beebium instance on the host, editable by hand or through the GUI, and
managed by CLI subcommands that the GUI invokes (the same pattern as the
preset subcommands).

### 2.1 Location and name

`<per-user Beebium directory>/aun-map.json`:

- macOS: `~/Library/Application Support/Beebium/aun-map.json`
- Windows: `%APPDATA%\Beebium\aun-map.json`
- Linux: `$XDG_CONFIG_HOME/beebium/aun-map.json` (or `~/.config/beebium/`)

The directory is the one `PresetPaths` and `DiscPaths` already agree on;
the private `user_state_base()` helper in `DiscPaths` becomes shared.
Overrides: `BEEBIUM_AUN_MAP_FILEPATH=<path>` for the environment, and
`--aun map-file=<path>` or `--aun map-file=none` for one launch (tests and
hermetic runs use `none`). For parity with `map=`, a subnet rule can be
given for one launch as `--aun subnet=128@192.168.1.0/24` (`Launch`
provenance). No other options: the file holds the standing configuration.

### 2.2 Format

JSON, as presets are, read with the parser the server already has:

```json
{
  "peers": [
    {"net": 0, "station": 254, "host": "192.168.1.10", "port": 32768,
     "label": "PiEconetBridge: file server 1.254 exposed as 0.254"},
    {"net": 0, "station": 40, "host": "192.168.1.40", "port": 32768,
     "label": "Archimedes A5000"},
    {"net": 0, "station": 41, "host": "risc-pc.local", "port": 32768,
     "label": "RISC PC"}
  ],
  "subnets": [
    {"net": 128, "subnet": "192.168.1.0/24",
     "label": "RISC OS convention: station = last octet, port 32768"}
  ]
}
```

Rules:

- `peers[]`: `net` (0..255, the Econet net byte; AUN convention puts AUN
  nets at 128 and above, and net 0 means "this machine's local net" as
  everywhere else in Beebium), `station` (1..254), `host` (an IPv4 literal
  or a DNS name), `port` (1..65535), optional `label`. One entry per
  `(net, station)`; a duplicate is a load error naming both entries.
- `subnets[]`: the RISC OS `!Internet` `AUNMap` rule, "net N is this /24,
  station is the last octet, port 32768". An entry means the whole
  convention, as the same line does on RISC OS: inbound, a packet from an
  unknown sender inside the subnet on port 32768 is accepted as net N
  station x; outbound, a frame for N.x with no explicit entry goes to the
  subnet's .x:32768, logged in the trace as a guess. There are no switches
  to take one half without the other; a user who wants explicit control
  writes `peers` and no `subnets`. It is the only form of net-level entry
  that can work (section 4). Optional; most files will have none.
- A DNS name is resolved when the file is loaded and on every reload. A
  name that does not resolve keeps its entry in the table as unreachable,
  shown as such, rather than being dropped.
- Unknown keys are preserved on rewrite and ignored on load, so a newer
  Beebium's file is not damaged by an older one. The loader reports a
  malformed file with its position and keeps the transport up.
- No comments: JSON has none, and the `label` field is where a note goes.
  A file written by the subcommands is pretty-printed with stable key
  order so hand edits and tool edits diff cleanly.

Beebium reads no BeebEm format. A one-time converter (section 2.5) turns a
BeebEm `Econet.cfg` and `AUNMap` into this file; BeebEm's mode and timing
keywords describe BeebEm's emulation, not the network, and are not carried
over.

### 2.3 Provenance and precedence

Provenance has five values (four landed with #55; `Subnet` arrives with
the file), and the sidebar names each:

| Provenance | Meaning | Sidebar |
|------------|---------|---------|
| `Api` | `AunService.AddPeer` at runtime | `API` |
| `Launch` | `--aun map=` / `subnet=` or the preset's equivalents, for this launch | `launch` |
| `MapFile` | the per-user `aun-map.json` | `map file` |
| `Discovered` | `_aun._udp` mDNS | `mDNS` |
| `Subnet` | derived from a `subnets` rule (inbound identification or an outbound guess) | `subnet` |

Precedence, highest first, in that order. An explicit instruction for this
process beats one for this launch, which beats the standing file, which
beats what the network says, which beats a convention. `Discovered` must
outrank `Subnet`: a Beebium at 192.168.1.40 advertising station 40 on port
40001 must not be sent packets at the convention's port 32768. "Operator
always wins over discovered" is preserved: the first three are operator
sources.

The one collision worth a rule: a discovered announcement for a
`(net, stn)` the file maps to a different endpoint. The file wins (the
operator wrote it down) and the disagreement is reported exactly as a
station collision is after #68: trace log, status count, sidebar line.
This catches the common mistake of a stale file entry for a Beebium
instance that now runs on another port.

### 2.4 Loading and reloading

- The AUN transport extension reads the file when it creates its backend
  and applies the entries as `MapFile` peers. Parsing lives in the
  extension, not the backend, so a malformed file is reported at launch
  with line numbers and the transport still comes up.
- Every instance polls the file's modification time on the subscriber's
  existing 2.5 s sweep (no platform file-watch code) and reloads on change:
  entries added, changed or removed in the file are added, replaced or
  removed in the table, touching only `MapFile` entries. Hand edits and GUI
  edits therefore reach every running instance within a few seconds, with
  no restart and no Break. (BeebEm re-reads on Break; polling is both
  simpler and more immediate.)
- `AunService.ReloadMap` forces a reload; `GetStatus` reports the file path,
  its entry count, and the last load error if any.

### 2.5 Management surface: RPCs, CLI subcommands, a converter

The server owns the file and is the only writer, through one library used
by every route below. The front end never touches the file: the server
may be on another machine, so a sidebar edit is an `AunService` RPC
(`AddMapPeer`, `RemoveMapPeer`, `AddMapSubnet`, `RemoveMapSubnet`,
`ListMap`, `ReloadMap`) and the server writes its own file. `GetStatus`
reports the file path on the server's host, its entry count and the last
load error.

For scripts and hand setup, following `list-presets` / `create-preset` /
`report-presets-dirpath`, any server executable offers:

| Subcommand | Does |
|------------|------|
| `report-aun-map-filepath` | prints the path (after overrides) |
| `create-aun-map` | writes a template at that path if absent: an empty `peers` list plus one example entry whose label explains the PiEconetBridge and RISC OS cases |
| `show-aun-map` | prints the parsed table with labels, resolution results, and any unknown keys |
| `add-aun-peer <net.stn> <host> <port> [--label <text>]` | adds or replaces the entry for that station |
| `remove-aun-peer <net.stn>` | removes that entry |
| `add-aun-subnet <net> <a.b.c.0/24> [--label <text>]`, `remove-aun-subnet <net>` | the same for subnet rules |

Writes are atomic (write a temporary, rename over) and preserve entry
order and unknown keys, so a hand-maintained file is not reshuffled by a
GUI edit. The map is model-independent, so it does not matter which
executable runs the subcommand.

**The BeebEm converter is not part of the server.** Beebium's core carries
no BeebEm dependency, even an implicit one in a file parser. The one-time
conversion is a standalone Python script, `tools/aun/convert_beebem_econet_cfg.py`
(run with `uv run`), that reads a BeebEm `Econet.cfg` and optionally its
`AUNMap`, turns the four-field host lines into `peers` and `ADDMAP` lines
into `subnets`, lists every keyword line it did not carry over, and writes
or merges an `aun-map.json`. BeebEm's shipped sample files are its test
fixtures.

### 2.6 GUI affordances (macOS, then any front end)

The AUN panel in the Network sidebar is server-driven (`AunUi` over
ExtensionRpc), so the affordances are built once in the extension:

- Each peer line gains its provenance text and label; `map file` entries
  gain Edit and Remove; an Add button opens the form that `AunUi` deferred
  ("Slice 3"). Edit, Remove and Add are `AunService` RPCs handled in the
  extension, which writes the file on the server's host; the running
  instance applies the change at once and other instances pick it up from
  the file poll. (The pre-launch-via-CLI rule does not apply: the machine
  is running, and the server may be remote.)
- Subnet rules list in their own small group under the peers ("net 128 =
  192.168.1.0/24, station = last octet, port 32768") with the same Add,
  Edit and Remove, since they live in the same file.
- Entries from other provenances are read-only there: a `launch` entry is
  changed by relaunching, an `API` entry by whoever added it, an `mDNS`
  entry by the network, a `subnet` entry by its rule.
- A "Save to map file" action on a `launch`, `API`, `mDNS` or `subnet`
  entry copies it into the file as an explicit peer: the way to pin a peer
  you can see working, and the natural way to graduate a guess into a
  record. (For an mDNS Beebium peer this pins an ephemeral port, which is
  why it is a deliberate action and not automatic; the sidebar warns.)

## 3. What this is not

- **Not BeebEm's station allocation.** BeebEm takes an instance's own station
  number from the first bindable local entry in `Econet.cfg`, in file order,
  so numbers depend on launch order. Beebium's station is set by `--station`,
  the preset, or the sidebar, and #67 (choose a free number from what mDNS
  shows) is the automatic alternative. An entry in the file for this
  machine's own station is ignored, as the mDNS self-filter ignores our own
  announcement, so a converted BeebEm file that lists every station loads
  cleanly.
- **Not a per-instance setting.** Anything that differs between two
  instances on one host (their own ports, test-only peers) stays on the
  command line or in the preset. The file is for the machines that are the
  same for every instance.
- **Not LEARN mode.** BeebEm's `LEARN` adds a sender to the table on receipt,
  with net 0 and station from the last IP octet, never written back and
  with a known clash risk. Beebium keeps its rule that an unknown sender is
  dropped (and trace-logged); discovery covers Beebium peers and the file
  covers the rest.
- **Not a general settings file.** Beebium has none, and this does not
  start one: the file holds peers and nothing else.

## 4. Why net-level entries are limited to the /24 convention

An AUN packet carries no destination station; the UDP endpoint identifies
the destination. So "all of net 1 via the bridge at 192.168.1.10:32768" is
not expressible: the bridge could not tell 1.254 from 1.1 on one port. A
PiEconetBridge `EXPOSE HOST` publishes each host on its own port, and each
needs its own line. The RISC OS convention, station = last octet of the
IP within a known /24 on port 32768, is the one net-level mapping with a
unique endpoint per station, so `ADDMAP` is supported and nothing else is
invented.

## 5. The PiEconetBridge case, as the acceptance test

mph1708's setup, and the recipe the Stardot answer needs. Bridge config
(the bridge's own terms):

```
FILESERVER ON 1.254 PATH /path/to/fs
AUN MAP HOST 2.80 ON <beebium-host> PORT 32768 NONE
EXPOSE HOST 1.254 ON PORT *:32768
```

`EXPOSE` lines must come last: upstream's config parser requires it.

Beebium side, by `--aun map=`: `--station 80 --aun
net=2:port=32768:map=1.254@<bridge-host>@32768` with an NFS or ANFS ROM in a
sideways slot. `*NET`, `*I AM 1.254 SYST` and `*CAT` then complete. With the
map file: `add-aun-peer 1.254 <bridge-host> 32768 --label
"PiEconetBridge FS"` (one `peers` entry in `aun-map.json`), then launch the
Station 80 AUN preset with `--aun net=2`, and every other instance on the host
sees the same entry.

The bridge's userspace daemon runs with no Pi and no kernel module when
the config has no wire net line (see
`pieconetbridge-aun-interop-testing.md`), Linux-only. The acceptance test is
`integration_tests/pieb-aun/tests/test_bridge_recipe.py`, run by the opt-in
`PiEconetBridge recipe` workflow (`.github/workflows/pieb-bridge-recipe.yml`):
`docker/pieconetbridge/run-recipe.sh` starts the daemon in a container with
host networking, rendered from the config above, and a Model B mapped by
`--aun map=` completes the login and the catalogue; a negative control with
the map entry removed fails with the filing system's no-reply error. The test
still maps the bridge with `--aun map=`; it was not switched to the map file
when #139 landed.

What the test ran differs from the recipe in two ways, both forced by running
both ends on one Linux host rather than on two:

- **Ports.** The bridge and Beebium cannot both bind 32768 on one host, so
  Beebium binds another port (40080 in the workflow) and the bridge's `AUN MAP
  HOST 2.80` line points at it. On separate hosts the recipe's 32768 on both
  sides stands.
- **No NAT.** The bridge matches Beebium's datagrams against its `AUN MAP HOST`
  line by source address and port, so the container uses host networking.
  Docker Desktop's port forwarder rewrites UDP source endpoints, so a native
  macOS or Windows Beebium cannot reach a containerised bridge with the recipe
  as written; a Linux Beebium in a container on the same host network can (the
  test passed that way under Docker Desktop on macOS, with the v0.5.0 Linux
  server).

The no-reply error is worded by the filing system ROM: "No reply" from NFS
3.34, "Station 1.254 not present" from ANFS 4.18, which the test uses. The
Piconet half of the question (Station 81 over a real Piconet adapter into the
bridge's wire) needs no AUN map at all and is tested on the physical bridge.

## 6. Sequencing

Issues: #139 (reader), #141 (RPCs and subcommands), #140 (converter), #142 (sidebar), #143 (bridge test).

1. `PeerSource` to four values, provenance text in the sidebar, and the
   doc fixes in section 7. Small; no proto change if the `AunPeerSource`
   enum in `aun.proto` is extended (ExtensionRpc, not the fingerprint).
2. File reader, `MapFile` and `Subnet` provenances and precedence, subnet
   rules (both halves), `subnet=` launch parity, mtime poll and reload,
   `map-file=` overrides, hostname resolution, `ReloadMap` and the status
   fields. Unit tests on the parser (BeebEm sample files as fixtures) and
   on precedence; the three-machine `[.mdns]` test extended with a file
   entry.
3. The map RPCs and CLI subcommands over the shared file-writing library;
   the standalone Python converter under `tools/aun/` with BeebEm's sample
   files as fixtures.
4. `AunUi` Add/Edit/Remove/Save-to-file over the map RPCs.
5. The containerised bridge acceptance test on the Linux lane.

Steps 1 to 3 are server-only and self-contained; step 2 alone answers
mph1708.

## 7. Cleanup found on the way

All four were addressed with step 1 (#55), which moved the peer table into
`AunEconetTransportExtension` as `AunPeerSet`:

- ~~`AunBackend.hpp` still says "there is no auto-discovery in this
  implementation".~~ Done: the comment now describes the backend as holding
  the resolved routing view set by the transport.
- ~~`aun-mdns-peer-discovery.md` says removing an operator entry lets the
  discovered one take over; `remove_peer` removes outright and the
  discovered peer returns only when re-announced.~~ Done: `AunPeerSet` keeps
  one entry per source, so removing the `Api` winner now falls back to a
  `Discovered` entry still present; code and doc agree.
- ~~`AunService.AddPeer` documents `net` as 0..127 but accepts up to 255.~~
  Done: the dispatcher then rejected `net > 127`; #139 later widened every
  net field to 0..255 (section 8), and it now rejects `net > 255`.
- ~~`econet-integration.md` says `AunService` is contributed through
  `grpc_services()`; it is served over ExtensionRpc.~~ Done: that doc now
  describes the `AunDispatcher` over `ExtensionRpc`.

## 8. Open questions

- (Resolved 2026-10-01: every net field is 0..255. The AUN code capped
  nets at 127 (`--aun net=`, `map=`, `AddPeer`) with no recorded reason,
  which made the RISC OS convention's nets of 128 and above unaddressable.
  Widened as part of #139; the `dest_net=0 -> local net` translation is
  unchanged.)

- Should `Launch` entries be written to the file on request only, or should
  a preset be able to say "also pin these peers in the map file"? Probably
  request only; presets are templates, the map is the user's.
- IPv6: AUN is IPv4 in every implementation we interoperate with; the file
  format allows a name that resolves to IPv6, which the backend would
  reject today. Defer with the rest of IPv6.
- (Resolved 2026-10-01: a `subnets` entry drives both inbound
  identification and outbound guessing, as on RISC OS, with no switches;
  see 2.2.)
