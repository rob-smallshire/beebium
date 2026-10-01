# A persistent AUN peer map file

Status: draft for discussion (2026-10-01). Prompted by mph1708's Stardot
question (https://stardot.org.uk/forums/viewtopic.php?p=493685#p493685):
how to link the Station 80 AUN preset to a PiEconetBridge that already
serves a wired Econet, a BeebEm laptop and a RISC OS Pi 400 over AUN.
Related: #55 (peer-table RPCs need an active backend), #54 (enable does not
bind), #67 (pick a free station from mDNS), #66 (multi-homed case 2).

## 1. Where the peer map comes from today

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
hermetic runs use `none`).

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

- `peers[]`: `net` (0..127), `station` (1..254), `host` (an IPv4 literal
  or a DNS name), `port` (1..65535), optional `label`. One entry per
  `(net, station)`; a duplicate is a load error naming both entries.
- `subnets[]`: the RISC OS `!Internet` `AUNMap` rule, "net N is this /24,
  station is the last octet, port 32768". It is the only form of net-level
  entry that can work (section 4). Optional; most files will have none.
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

`PeerSource` grows from two values to four, and the sidebar names each:

| `PeerSource` | Meaning | Sidebar |
|--------------|---------|---------|
| `Launch` | `--aun map=` or the preset's `map` for this launch | `launch` |
| `Api` | `AunService.AddPeer` at runtime | `API` |
| `MapFile` | the per-user `aun-map.json` | `map file` |
| `Discovered` | `_aun._udp` mDNS | `mDNS` |

Precedence, highest first: `Api`, `Launch`, `MapFile`, `Discovered`. An
explicit instruction for this process beats one for this launch, which
beats the standing file, which beats what the network says. Today's
"operator always wins over discovered" is preserved: the first three are
all operator sources.

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

### 2.5 Management surface: CLI subcommands

Following `list-presets` / `create-preset` / `import-preset` /
`report-presets-dirpath`, any server executable offers:

| Subcommand | Does |
|------------|------|
| `report-aun-map-filepath` | prints the path (after overrides) |
| `create-aun-map` | writes a template at that path if absent: an empty `peers` list plus one example entry whose label explains the PiEconetBridge and RISC OS cases |
| `show-aun-map` | prints the parsed table with labels, resolution results, and any unknown keys |
| `add-aun-peer <net.stn> <host> <port> [--label <text>]` | adds or replaces the entry for that station |
| `remove-aun-peer <net.stn>` | removes that entry |
| `convert-beebem-econet-cfg <Econet.cfg> [<AUNMap>] [--output <path>]` | one-time conversion: the four-field host lines become `peers`, `ADDMAP` lines become `subnets`, every keyword line is listed as not carried over; writes a new map file or merges into the existing one, reporting each entry taken |

Writes are atomic (write a temporary, rename over) and preserve entry
order and unknown keys, so a hand-maintained file is not reshuffled by a
GUI edit. The map is model-independent, so it does not matter which
executable runs the subcommand.

### 2.6 GUI affordances (macOS, then any front end)

The AUN panel in the Network sidebar is server-driven (`AunUi` over
ExtensionRpc), so the affordances are built once in the extension:

- Each peer line gains its provenance text and label; `map file` entries
  gain Edit and Remove; an Add button opens the form that `AunUi` deferred
  ("Slice 3"). Edit, Remove and Add dispatch to the CLI subcommands above
  through the app (the rule that GUIs invoke subcommands), then the file
  poll picks up the change.
- Entries from other provenances are read-only there: a `launch` entry is
  changed by relaunching, an `API` entry by whoever added it, an `mDNS`
  entry by the network.
- A "Save to map file" action on a `launch`, `API` or `mDNS` entry copies it
  into the file: the way to pin a peer you can see working. (For an mDNS
  Beebium peer this pins an ephemeral port, which is why it is a deliberate
  action and not automatic; the sidebar warns.)

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
EXPOSE HOST 1.254 ON PORT *:32768
AUN MAP HOST 2.80 ON <beebium-host> PORT 32768 NONE
```

Beebium side, once: `add-aun-peer 1.254 <bridge-host> 32768 --label "PiEconetBridge FS"`
(one `peers` entry in `aun-map.json`),
then launch the Station 80 AUN preset with `--aun net=2`. `*I AM 1.254 SYST`
works, and every other instance on the host sees the same entry.

The bridge's userspace daemon runs with no Pi and no kernel module when
the config has no wire net line (see
`pieconetbridge-aun-interop-testing.md`), Linux-only, so the automated
test runs the daemon in a container on the Linux lane and a Beebium
instance mapping it by file completes a file-server transaction through it.
The same harness validates the published recipe. The Piconet half of the
question (Station 81 over a real Piconet adapter into the bridge's wire)
needs no AUN map at all and is tested on the physical bridge.

## 6. Sequencing

1. `PeerSource` to four values, provenance text in the sidebar, and the
   doc fixes in section 7. Small; no proto change if the `AunPeerSource`
   enum in `aun.proto` is extended (ExtensionRpc, not the fingerprint).
2. File reader, `MapFile` provenance and precedence, mtime poll and reload,
   `map-file=` overrides, hostname resolution, `ReloadMap` and the status
   fields. Unit tests on the parser (BeebEm sample files as fixtures) and
   on precedence; the three-machine `[.mdns]` test extended with a file
   entry.
3. CLI subcommands including the BeebEm converter, with the shipped BeebEm
   `Econet.cfg` and `AUNMap` samples as conversion fixtures.
4. `AunUi` Add/Edit/Remove/Save-to-file, dispatched through the app to the
   subcommands.
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
  `Discovered` entry still present — code and doc agree.
- ~~`AunService.AddPeer` documents `net` as 0..127 but accepts up to 255.~~
  Done: the dispatcher now rejects `net > 127`.
- ~~`econet-integration.md` says `AunService` is contributed through
  `grpc_services()`; it is served over ExtensionRpc.~~ Done: that doc now
  describes the `AunDispatcher` over `ExtensionRpc`.

## 8. Open questions

- Should `Launch` entries be written to the file on request only, or should
  a preset be able to say "also pin these peers in the map file"? Probably
  request only; presets are templates, the map is the user's.
- IPv6: AUN is IPv4 in every implementation we interoperate with; the file
  format allows a name that resolves to IPv6, which the backend would
  reject today. Defer with the rest of IPv6.
- Whether `ADDMAP` nets should also drive outbound guessing for stations
  not listed (BeebEm's `AUNSTRICT`), or only inbound identification. Both
  are what RISC OS does; start with both and report each guess in the
  trace.
