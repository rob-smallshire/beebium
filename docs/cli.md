# Beebium Command Line Interface

The Beebium emulator runs as a headless gRPC server. This document covers all command-line options for the server executables.

## Executables

| Executable | Machine | Description |
|------------|---------|-------------|
| `beebium-model-b` | BBC Model B | Original 32K BBC Micro with MOS 1.20 |
| `beebium-model-b-plus` | BBC Model B+ 64K | Enhanced 64K model with MOS 2.0 |
| `beebium-model-b-romram` | BBC Model B with ROM/RAM expansion | Notional 16-slot ROM/RAM expansion board over an MOS 1.20 Model B |
| `beebium-model-b-atpl-sidewise` | BBC Model B with ATPL Sidewise | The historical ATPL Sidewise board: 16 slots, slot 15 a RAM/ROM socket with write-through and a runtime write-protect switch |
| `beebium-model-b-watford-rom-ram` | BBC Model B with Watford ROM/RAM board | The historical Watford Electronics board: sideways RAM banks 0-7, a `&FF30` write-select latch, S1/S2 protection switches |
| `beebium-model-b-integra-b` | BBC Model B with Integra-B | The Computech Integra-B board with IBOS: sideways RAM banks 4-7, ROM/RAM socket pairs 8-15, shadow and private RAM, real-time clock, per-chip write-protect switches (see `docs/integra-b/README.md`) |

## Usage

```bash
<executable> [global-options] <subcommand> [subcommand-options]
```

If no subcommand is specified, `start` is assumed.

## Global Options

| Option | Description |
|--------|-------------|
| `--help`, `-h` | Show global help message |
| `--format <format>` | Output format for data commands (see below) |

### Output Formats

The `--format` option controls output formatting for data commands (`list-fdcs`, `describe-machine`):

| Format | Description | Auto-selected when |
|--------|-------------|-------------------|
| `pretty` | Human-friendly formatted output | stdout is TTY |
| `tsv` | Tab-separated values with header row | stdout is not TTY |
| `jsonl` | JSON Lines (one JSON object per line) | Never (explicit only) |

If `--format` is not specified, the format is auto-detected based on whether stdout is a TTY:
- **TTY (interactive terminal)**: Uses `pretty` format
- **Not TTY (piped/redirected)**: Uses `tsv` format

Examples:
```bash
# Auto-detect format (pretty in terminal, tsv when piped)
beebium-model-b list-fdcs
beebium-model-b list-fdcs | cat     # tsv output

# Explicit format
beebium-model-b --format pretty list-fdcs
beebium-model-b --format tsv describe-machine
beebium-model-b --format jsonl list-fdcs
```

## Integer Formats

All integer arguments accept multiple formats:

| Format | Prefix | Example | Value |
|--------|--------|---------|-------|
| Decimal | (none) | `48875` | 48875 |
| Hexadecimal | `0x` or `0X` | `0xBEEB` | 48875 |
| Binary | `0b` or `0B` | `0b1010` | 10 |
| Octal | `0o` or `0O` | `0o377` | 255 |

This applies to `--port`, `--screen-mode`, `--links` (the keyboard
startup-options byte; not to be confused with `--motherboard-link`,
which takes named string values), slot numbers in `--sideways`, and
drive numbers in `--floppy`.

## Subcommand Naming

**Design principle**: All subcommand names must include at least one verb. This ensures commands clearly communicate their action.

| Good | Bad | Reason |
|------|-----|--------|
| `list-presets` | `presets` | Verb "list" indicates action |
| `describe-machine` | `machine-info` | Verb "describe" indicates action |
| `report-presets-dirpath` | `presets-dir` | Verb "report" indicates action |
| `create-preset` | `new-preset` | "new" is an adjective, not a verb |
| `delete-preset` | `preset-remove` | Verb should come first |

## Subcommands

### start

Start the emulator server. This is the default subcommand.

```bash
beebium-model-b start [options]
beebium-model-b [options]           # Equivalent (start is default)
```

#### Presets

| Option | Description |
|--------|-------------|
| `--preset <id\|filepath>` | Load configuration from a preset. CLI options override preset values |

The argument is first tried as a file path. If no such file exists it is resolved as a preset id, the same way as `show-preset`: the system presets directory first, then the user presets directory. `list-presets` shows the ids available. If neither lookup succeeds the server exits with `NOINPUT` (66):

```
Error: Preset not found: 'x' (no such file, and no system or user preset with that id)
```

```bash
beebium-model-b --preset model-b-disc                  # built-in preset by id
beebium-model-b --preset ./my-game.preset.beebium      # preset file by path
```

CLI-overrides-preset applies to the Econet transport too. A `--aun` (or
`--piconet`) on the command line overrides the preset's `econet.transport`
rather than counting as a second transport: the same transport name merges
parameters with the CLI winning per key, and a different transport name
replaces the preset's outright. The CLI's `--aun` already carries the
manifest default of every parameter it does not name (`port=32768`, `net=0`,
`discovery=on`), and those defaults win too, so only the default-less keys
(`map`, `subnet`, `map-file`) are kept from the preset. Repeat on the command
line any defaulted key the preset sets: `--preset model-b-disc-aun-80 --aun
port=0:discovery=off:map-file=none` takes all three from the CLI, while
`--aun map-file=none` alone would also replace the preset's `port=0` with
32768 and its discovery setting with `on`.

#### ROM Configuration

| Option | Description |
|--------|-------------|
| `--mos <filepath>` | Path to MOS ROM (default: machine-specific) |
| `--language-rom <filepath>` | Language ROM image for the machine's own default language slot (default: machine-specific). Use this instead of `--sideways` when you don't want to assume which slot holds the language ROM - e.g. the ATPL Sidewise uses slot 14, others use slot 15 |
| `--sideways slot=<0-15>:type=<rom\|ram\|empty>[:image=<path>][:write-protect]` | Configure a sideways slot (repeatable; see below) |
| `--rom-dir <dirpath>` | ROM directory (auto-detected if not specified) |

`--sideways` takes colon-separated `key=value` pairs (the same convention
as the extension args, e.g. `--aun net=0:port=32768`). Quote a value that
itself contains a colon, e.g. `image="C:\roms\a.rom"`.

- `slot=<0-15>` and `type=<rom|ram|empty>` are required.
- `image=<path>` is required for `rom`, optional for `ram` (a pre-load), and
  forbidden for `empty`.
- `write-protect` is a bare flag (RAM only) that engages the slot's
  write-protect switch at startup - its power-on position. Accepted only for
  a socket that has such a switch (`SocketCapabilities.supports_write_protect`,
  e.g. the ATPL Sidewise slot 15); rejected for RAM with no switch (e.g. the
  B+ 128K SRAM banks). The switch is also runtime-toggleable via
  `SidewaysService.SetSlotWriteProtect`.

The former positional `SLOT:TYPE[:IMAGE]` form is no longer accepted; the
server rejects it with a message pointing at the key=value grammar.

`--sideways` arguments are validated against each machine variant's
slot topology at startup. The server refuses to start when an argument
references a slot that does not exist on the chosen machine, requests
a type the underlying physical socket does not support (e.g. `ram` on
a Model B+, where every socket is ROM-only), targets two aliased slots
with conflicting types or different ROM images, or specifies the same
slot twice. All detected problems are reported in a single error
message naming the affected socket and its alias set, so multiple
mistakes can be fixed before retrying. The full topology and
validation rules per machine are described in
[sideways-slots.md](sideways-slots.md).

#### Disc Configuration

| Option | Description |
|--------|-------------|
| `--floppy <drive>:<filepath\|url>` | Load disc image into drive 0 or 1 |
| `--fdc <type>` | Disc controller to install (Model B only) |

The `--floppy` option accepts file paths (most common) or `file://` URLs.

Disc controller types for `--fdc`:
- `acorn-1770` - Acorn WD1770 controller
- `none` - Leave socket empty (no disc)

#### gRPC Server

| Option | Default | Description |
|--------|---------|-------------|
| `--port <port>` | 48875 (0xBEEB) | gRPC server port |

Use `--port 0` to request dynamic port allocation. The server prints the allocated port to stdout:

```
Starting gRPC server...
Listening on port 54321
```

Scripts can parse the `Listening on port <N>` line to discover the allocated port.

#### Service Advertisement (mDNS)

| Option | Default | Description |
|--------|---------|-------------|
| `--advertise` | off | Advertise this server over mDNS (DNS-SD `_beebium._tcp`) so frontends auto-discover it |

Advertisement is opt-in. With `--advertise`, the server publishes an
`_beebium._tcp` record on the local network carrying its host, port, and TXT
metadata (`uuid`, `model`, `role`, `provenance`); a discovering frontend (the
macOS app today) lists it without the user typing `host:port`. The advertised
instance name is the machine's display name (`--machine-name`).

Platform support: macOS (Bonjour) and Windows (Win32 DNS-SD) are built in; Linux
uses Avahi, which is loaded at runtime, so a running `avahi-daemon` is required
and the flag becomes a silent no-op where Avahi is absent. See
[networking.md](networking.md) and
[plans/service-advertisement.md](plans/service-advertisement.md) for details.

#### Econet

Both AUN and Piconet are Econet *transport extensions* dispatched
through the same generic CLI mechanism that drives peripheral
extensions like `--acorn-rtc`. AUN is a built-in extension; Piconet
ships as a discoverable plugin under `src/extensions/piconet/`.

| Option | Default | Description |
|--------|---------|-------------|
| `--station <1-254>\|auto[:lo-hi]` | (omitted = no Econet) | Econet station number; presence enables Econet hardware. Without a transport extension the ADLC reports "No Clock". `auto` (or `auto:lo-hi`, default range 1-253; the keyword is case-insensitive) makes the transport choose a free number at launch -- see **Automatic station number** below. A number or range outside 1-254, or `lo` above `hi`, is a usage error (exit 64). |
| `--aun [key=value[:key=value...]]` | — | AUN UDP transport. Parameters are listed below; `describe-extension aun` prints the same list. |
| `--piconet [device_path=<path>]` | — | Piconet USB-CDC bridge to a real Econet wire. `device_path` names the serial device; omit it (or give `auto`) to find a connected Piconet by its USB vendor id and a STATUS probe. Mutually exclusive with `--aun`. |

`--aun` parameters (colon-separated, the generic extension grammar):

| Key | Default | Meaning |
|-----|---------|---------|
| `port=<n>\|none` | `32768` | UDP port to bind. `0` lets the OS choose a free port (the announcement and `AunService.GetStatus` report the real one). `none` fits the hardware with no network. A value that is not a number in 0-65535 is reported on stderr and the default 32768 is used. |
| `net=<0-255>` | `0` | The local Econet net number. An invalid value is reported on stderr and 0 is used. |
| `map=<net.stn@ip@port>` | — | A peer for this launch (repeatable; provenance Launch). The inner separator is `@` (shell-safe in every common shell, and non-colliding with the `.` inside IPv4 / `net.stn`); `ip` is an IPv4 literal; a bare `stn` means net 0. A malformed entry is reported on stderr and skipped. |
| `subnet=<net@a.b.c.0/24>` | — | A subnet rule for this launch (repeatable; the RISC OS convention: station is the last octet, port 32768). Only `/24`; the last octet of the address is ignored. A malformed entry is reported and skipped. |
| `map-file=<path>\|none` | the per-user `aun-map.json` | The map file to load and edit. Precedence: this key, then `BEEBIUM_AUN_MAP_FILEPATH`, then the per-user default (`report-aun-map-filepath` prints it). `none` disables the map file. |
| `discovery=on\|announce\|browse\|off` | `on` | mDNS on `_aun._udp`: `announce` publishes without adopting, `browse` adopts without publishing, `off` neither -- see **Discovery mode** below. Any other value is a launch error (exit 64). |

**Transport selection:**

- **`--aun port=<n>`:** Talk to other AUN-speaking peers (other Beebium instances, BeebEm, PiEconetBridge) over UDP/IP. The peer table is filled from `map=` / `subnet=` entries, the map file, `AunService.AddPeer`, and (unless `discovery` says otherwise) other instances' `_aun._udp` announcements.
- **`--piconet device_path=<path>`:** Talk to real BBCs / Acorn fileservers / printers / etc. over a real Econet wire via the [Piconet](https://github.com/jprayner/piconet) USB device. The wire's clock generator and termination must be present; the Piconet is a participant on the wire, not a clock source.
- **`--aun port=none`:** Hardware fitted, no transport. Useful for testing the NFS ROM's "No Clock" path or for keeping a station number reserved without networking.
- **No transport flag (just `--station`):** Econet hardware fitted but no transport configured — the ADLC sees no carrier (DCD high). Identical to the `port=none` case.
- **No `--station`:** Econet hardware not fitted at all. The `&FE18` station ID register returns 0x00 (open bus); NFS ROM detects no Econet.

**Discovery mode (`--aun discovery=`).** `on` (default) announces and browses `_aun._udp`; `announce` publishes without adopting; `browse` adopts without publishing; `off` does neither (only `map=` / the map file / `AddPeer` populate the table). Collision reporting and parked adoption need browsing, so they are silent under `announce` and `off`; with `off` or `announce`, `--station auto` resolves immediately from the map file, launch `map=` peers and the per-host hint with no network wait, and with `browse` a same-instant auto race is not arbitrated. The sidebar shows the mode when it is not `on`, and `AunService.GetStatus` reports `discovery_mode` (over ExtensionRpc; the protocol fingerprint is unchanged). Full semantics in [networking.md](networking.md#discovery-mode---aun-discovery-issue-158).

**Automatic station number (`--station auto`).** `--station auto` (or `auto:<lo>-<hi>`, default 1-253), or a preset's `"econet": {"station": "auto"}`, makes the transport pick a free station number at launch -- before the machine runs and before the socket is enabled, so the guest reads its final number at its first boot and never needs a Break. The number is chosen by browsing `_aun._udp` for the numbers in use on this net (plus map-file and launch `map=` peers, not subnet guesses), taking the lowest free one, and briefly settling a same-second race so instances started together land on distinct numbers. It is bounded (a few seconds) and runs off the emulation thread; if the whole range is in use it starts at the range's first number and says so on stderr rather than failing. Only the AUN transport supports it (Piconet bridges a real wire with no announcements to consult, and rejects `auto`; so do `--aun port=none` and a bare `--station auto` with no transport -- these are reported once the server has printed its port, and it then exits with status 1); the bundled `model-b-disc-aun-auto` preset uses it. The chosen number is the socket's real station number, so every surface (status, sidebar, the `_aun._udp` announcement) shows it, and the launch log notes it as `(auto-assigned)`. Selection runs *after* the server is listening and has printed its port (so it never delays a launcher's port-wait); with `--wait=api` the first `Run()` waits for it. A **staggered** launch is reliable; a **same-instant** launch is best-effort — a residual clash surfaces as an ordinary station-number collision (renumber one). The timings are tunable via `BEEBIUM_AUN_AUTO_MIN_OBSERVE_MS` / `_QUIET_MS` / `_BUDGET_MS` (defaults 1500 / 600 / 4000). Build-time boots (`capture-screenshot`, preset generation) take the range's first number with no browse, for determinism. To space re-use out, the search starts one past the last number this host allocated (wrapping), recorded in a small per-user hint file; `BEEBIUM_AUN_AUTO_STATE_FILEPATH` overrides its path and `none` disables it (plain lowest-free). The hint never fails a launch -- any error falls back to lowest-free. See [networking.md](networking.md#choosing-a-free-station-number-at-launch---station-auto-issue-67).

**Examples:**

```bash
# Two Beebium instances on loopback, mapped to each other explicitly
beebium-model-b --station 32 --aun port=32768:map=0.254@127.0.0.1@32769

beebium-model-b --station 254 --aun port=32769:map=0.32@127.0.0.1@32768

# Hermetic: OS-chosen port, no mDNS, no map file
beebium-model-b --station 32 --aun port=0:discovery=off:map-file=none

# Pick a free station number in 40-49 at launch
beebium-model-b --station auto:40-49 --aun port=0

# Talk to real Econet via Piconet
beebium-model-b --station 250 --piconet device_path=/dev/tty.usbmodem101

# Econet board fitted but no transport configured
beebium-model-b --station 32 --aun port=none
# (or just: beebium-model-b --station 32)
```

Both transports are configured in presets via the generic
`econet.transport` object:

```json
"econet": {
  "station": 32,
  "transport": {
    "name": "aun",
    "parameters": {
      "port": "32768",
      "discovery": "on",
      "map": ["0.254@127.0.0.1@32769", "0.253@127.0.0.1@32770"]
    }
  }
}
```

`station` is an integer 1-254 or the string `"auto"` / `"auto:lo-hi"`.
The `name` field selects the transport extension (`aun` or `piconet`);
`parameters` holds the same keys as the CLI parameters. A repeatable key
(`map`, `subnet`) takes a JSON array of strings, or a single string for one
entry. CLI arguments override preset values, including the transport (see
**Presets** above). Two transports on the command line -- `--aun ...` and
`--piconet ...` -- are rejected as "BBC machines support at most one Econet
transport (got 2)."

**Migration from older flags:** the legacy `--aun-port`,
`--aun-map`, and bare `--piconet <path>` flags have been removed
(they are now unknown arguments, exit 64). Update preset files to the
`transport` shape above: a preset with `econet.aun_port` or
`econet.piconet` fails to load with a message pointing at the new form;
an `econet.aun_map` key is ignored, like any other unknown key.

See `docs/networking.md` for the architecture and `docs/discussion/piconet-feasibility.md` for the Piconet design.

#### Startup Control

| Option | Description |
|--------|-------------|
| `--wait` | Delay emulation start (mode auto-detected) |
| `--wait=cli` | Wait for RETURN keypress on stdin |
| `--wait=api` | Wait for `Run()` RPC from client |

The `--wait` option allows clients to connect and set up before emulation begins.

**`--wait` (bare)**: Auto-detects mode based on whether stdin is a TTY:
- **TTY detected**: Uses `cli` mode (waits for RETURN)
- **No TTY**: Uses `api` mode (waits for Run() RPC)

**`--wait=cli`**: Waits for the user to press RETURN on the console before starting emulation.

**`--wait=api`**: Pauses the machine immediately after the 6502 reset sequence completes (7 cycles). Call `DebuggerControl/Run` to start emulation.

#### Startup Options (Keyboard Links)

| Option | Description |
|--------|-------------|
| `--screen-mode <0-7>` | Startup screen mode (default: 7) |
| `--auto-boot` | Reverse SHIFT-BREAK behavior (SHIFT-BREAK boots disc) |
| `--links <0-255>` | Raw startup options byte |

`--links` is mutually exclusive with `--screen-mode` and `--auto-boot`.

#### Motherboard Links

| Option | Description |
|--------|-------------|
| `--motherboard-link KEY=VALUE` | Set a motherboard jumper position (case-insensitive). Repeat the flag for multiple links. |

This option is distinct from the keyboard `--links` byte above:
keyboard links live on the keyboard PCB and select startup behaviour;
**motherboard** links are physical jumpers on the main board that
change which slots the sideways ROM sockets respond to. The set of
available links is machine-specific; on machines with no slot-mapping
links (Model B, ROM/RAM board) the option is rejected at parse time
and is hidden from `start --help`.

Currently modelled:

| Machine | KEY | Values | Effect |
|---------|-----|--------|--------|
| Model B+ | `s13` | `west` (default), `east` | `west`: IC71 (BASIC) socket appears at slots 14/15; `east`: at slots 0/1. The other pair becomes electrically dead. |

The link state is set once at server start; there is no runtime API
to change it. Servers report their configured link state to clients
via `SidewaysService.GetSlotStatus`, so frontends can display the
current configuration without re-parsing CLI arguments.

Example:

```bash
beebium-model-b-plus --motherboard-link s13=north --sideways slot=0:type=rom:image=bbc-basic_2.rom
```

See [sideways-slots.md](sideways-slots.md) for the full slot topology
behind these options.

#### Board Real-Time Clock

| Option | Description |
|--------|-------------|
| `--integra-rtc KEY=VALUE[:KEY=VALUE...]` | Configure the Integra-B's built-in CDP6818 real-time clock (only `beebium-model-b-integra-b` accepts it). |

| KEY | Values | Effect |
|-----|--------|--------|
| `clock` | `host` (default), `emulated` | `host`: the calendar follows the host's local time, so it keeps real time across host sleep and pauses. `emulated`: it advances only with emulated CPU cycles, so everything the clock does is a deterministic function of emulated time (reproducible sessions and tests). |
| `time` | `YYYY-MM-DDThh:mm[:ss]` or `YYYY-MM-DDThhmm[ss]` | Start the clock at this local time. |
| `offset` | e.g. `-10y`, `+5h`, `-1y6M` (units `y M d h m s`) | Start the clock this far from the host's local time. Mutually exclusive with `time`. |

The guest can still set the time itself (IBOS `*TIME=` / `*DATE=`); the
clock then runs on from what it set, under either time source.

Examples:

```bash
# Deterministic: start at 10:20:30 on 15 September 2026, advance with emulated time
beebium-model-b-integra-b --integra-rtc clock=emulated:time=2026-09-15T10:20:30
# Host time, but ten years ago
beebium-model-b-integra-b --integra-rtc offset=-10y
```

### list-fdcs

List available disc controllers that can be installed in machines with a disc controller socket.

```bash
beebium-model-b list-fdcs
```

Output varies by format (see [Output Formats](#output-formats)):

**`--format pretty`** (default for TTY):
```
Available disc controllers:
  acorn-1770 - Acorn 1770 (WD1770)
      Standard Acorn disc controller upgrade for BBC Model B
  none - No disc controller (leave socket empty)
```

**`--format tsv`** (default for non-TTY):
```
id	display_name	fdc_chip	description
acorn-1770	Acorn 1770	WD1770	Standard Acorn disc controller upgrade for BBC Model B
none	No controller	-	Leave socket empty (no disc)
```

**`--format jsonl`**:
```
{"id":"acorn-1770","display_name":"Acorn 1770","fdc_chip":"WD1770","description":"Standard Acorn disc controller upgrade for BBC Model B"}
{"id":"none","display_name":"No controller","fdc_chip":"-","description":"Leave socket empty (no disc)"}
```

### list-extensions

List every peripheral or transport extension that the server can recognise via a `--<cli-name>` flag, in the same priority order that `start` would resolve. See [Extension Search Paths](peripheral-extension-framework.md#extension-search-paths) in the extension framework doc for the full ordering rules.

```bash
beebium-model-b list-extensions
beebium-model-b list-extensions --extension-dir ~/my-beebium-plugins
beebium-model-b list-extensions --attaches-to serial-port
```

`--extension-dir` is repeatable; later paths override earlier ones (and the auto-detected `<exe-dir>/extensions`) for matching `cli` names.

`--attaches-to <point>` shows only extensions that attach to the given attachment point (e.g. `serial-port`); see [list-attachment-points](#list-attachment-points) for the available points. An extension may attach to several points, so it appears under each.

**`--format pretty`** (default for TTY):
```
Available extensions:
  --tube-65c02 (acorn-65c02-coprocessor) [built-in]
      Acorn 65C02 3 MHz second processor
  --acorn-rtc [/install/path/extensions]
      Acorn User Port Real Time Clock Module (SAF3019P)
  ...
```

**`--format tsv`** (default for non-TTY):
```
cli_name	name	kind	source	attaches_to	description
tube-65c02	acorn-65c02-coprocessor	peripheral	built-in	tube	Acorn 65C02 3 MHz second processor
acorn-rtc	acorn-rtc	peripheral	/install/path/extensions	user-port	Acorn User Port Real Time Clock Module (SAF3019P)
```

**`--format jsonl`** — one JSON object per extension, with the same fields as the TSV form (`attaches_to` is a JSON array).

A `--extension-dir` argument that points to a non-existent directory is a hard error (exit code `EX_CONFIG` = 78). The auto-detected default is silent if absent.

### describe-extension

Show the parameter schema for a single extension. The argument matches against either the CLI flag stem (e.g. `tube-65c02`) or the canonical extension name (e.g. `acorn-65c02-coprocessor`).

```bash
beebium-model-b describe-extension acorn-rtc
beebium-model-b describe-extension --format jsonl scsi-hdd
```

**`--format pretty`** prints a parameter list with type, default, and description per parameter; `tsv` and `jsonl` emit one row per parameter.

Like `list-extensions`, `--extension-dir` is repeatable and can be used to look up extensions in user-supplied directories.

### list-attachment-points

List the attachment points an extension can plug into (the ids used by `attaches_to`), each with a display name and its occupancy. Pair it with `list-extensions --attaches-to <point>` to drive a configuration UI: enumerate the points, then for each point offer the extensions that attach to it.

```bash
beebium-model-b list-attachment-points
```

**`--format pretty`**:
```
Attachment points:
  serial-port (Serial Port) [0..1]
      The BBC RS423 serial port (MC6850 ACIA + Serial ULA).
  1mhz-bus (1 MHz Bus) [0..N]
      The 1 MHz bus expansion connector (FRED/JIM paged I/O).
  ...
```

Occupancy is an integer range `[min..max]`: how many extensions must and may attach. A connector is `0..1` (optional, at most one -- so the UI offers "which **one**, if any"); a bus is `0..N` (`N` = no upper bound, shown as `null` in jsonl). The range accommodates hardware that is not simply single-or-many -- e.g. a future twin-Tube machine would be `0..2`. `tsv` and `jsonl` carry `id`, `display_name`, `min_occupancy`, `max_occupancy` (blank/`null` when unbounded), and `description`. Like the other discovery commands, this needs no running emulator.

### describe-machine

Output machine information for programmatic use.

```bash
beebium-model-b describe-machine
```

Output varies by format (see [Output Formats](#output-formats)):

**`--format pretty`** (default for TTY):
```
Machine:        BBC Model B
Executable:     beebium-model-b
Version:        0.1.0
MOS ROM:        acorn-mos_1_20.rom
Language ROM:   bbc-basic_2.rom (slot 15)
```

**`--format tsv`** (default for non-TTY):
```
key	value
machine_type	ModelB
display_name	BBC Model B
executable	beebium-model-b
version	0.1.0
default_mos_rom	acorn-mos_1_20.rom
default_language_rom	bbc-basic_2.rom
default_language_slot	15
```

**`--format jsonl`**:
```
{"executable":"beebium-model-b","machine_type":"ModelB","display_name":"BBC Model B","version":"0.1.0","default_mos_rom":"acorn-mos_1_20.rom","default_language_rom":"bbc-basic_2.rom","default_language_slot":15}
```

Model B+ also includes DFS information (`default_dfs_rom`, `default_dfs_slot`).

### describe-preset-schema

Output the configuration schema for presets. Used by GUIs to build dynamic configuration UIs.

```bash
beebium-model-b describe-preset-schema
```

Output is JSON describing the model and available configuration sections. See [preset-system.md](plans/preset-system.md) for schema details.

### Preset Management Subcommands

These subcommands manage preset files. GUIs invoke these rather than implementing preset logic directly, ensuring consistent behavior across all clients.

#### list-presets

List available presets for this model.

```bash
beebium-model-b [--format pretty|tsv|jsonl] list-presets [--json]
```

The output follows the global `--format` option: pretty on a terminal, TSV when piped. `--json` is an alias of `--format jsonl`.

**`pretty`** (the id column is as wide as the longest id plus a two-space gap):
```
Built-in presets:
  model-b                 BBC Model B
  model-b-with-acorn-dfs  BBC Model B with Acorn DFS

User presets:
  my-elite-setup          My Elite Setup
```

**`tsv`**:
```
id	name	source
model-b	BBC Model B	system
model-b-with-acorn-dfs	BBC Model B with Acorn DFS	system
my-elite-setup	My Elite Setup	user
```

**`jsonl`** (one object per preset):
```json
{"id":"model-b","name":"BBC Model B","source":"system"}
{"id":"my-elite-setup","name":"My Elite Setup","source":"user"}
```

#### show-preset

Output the contents of a preset file.

```bash
beebium-model-b show-preset <id>
```

Outputs the preset JSON to stdout. Useful for inspection or piping to editors.

#### report-presets-dirpath

Report the directory path where user presets are stored.

```bash
beebium-model-b report-presets-dirpath
```

Output (platform-specific):
```
/Users/alice/Library/Application Support/Beebium/presets
```

This path can be overridden with the `BEEBIUM_USER_PRESETS_DIRPATH` environment variable.

#### create-preset

Create a new preset.

```bash
beebium-model-b create-preset --name "My Elite Setup" [--from <source-id>] [--output <path>]
```

| Option | Description |
|--------|-------------|
| `--name <name>` | Display name for the preset (required) |
| `--from <id>` | Source preset to copy configuration from (optional) |
| `--output <path>` | Write preset to specified path instead of user presets directory |
| `--station <1-254>\|auto[:lo-hi]` | Fit Econet, recorded as `econet.station` (`auto` is recorded verbatim and resolved at each launch) |
| `--aun ...` / `--piconet ...` | Recorded as the preset's `econet.transport`, same grammar as `start`. Requires `--station`; at most one transport |

The preset ID is derived by slugifying the name. Outputs the created preset ID (or path if `--output` used) on success:
```
my-elite-setup
```

`create-preset` also accepts `--description`, `--machine-name`, `--auto-boot`, `--release-date`, `--fdc`, `--sideways` and other `--<extension>` flags; `create-preset --help` lists them. The recorded transport parameters include the manifest defaults the CLI fills in, e.g. `--station auto:40-49 --aun port=0:discovery=off` records `"station": "auto:40-49"` and `{"discovery": "off", "net": "0", "port": "0"}`.

If `--from` is omitted, creates a minimal "bare" preset for the model containing just the model ID, name, and release date.

**Build system usage**: CMake uses `create-preset --output` to generate bare preset files at build time:
```bash
beebium-model-b create-preset --name "BBC Model B" --output presets/model-b.preset.beebium
```

#### delete-preset

Delete a user preset.

```bash
beebium-model-b delete-preset <id>
```

Only user presets can be deleted. Attempting to delete a system preset returns an error.

#### import-preset

Import a preset file into the user presets directory.

```bash
beebium-model-b import-preset <filepath>
```

Validates the preset file and copies it to the user presets directory. Handles ID conflicts by appending numbers (e.g., `elite-setup-2`).

Outputs the imported preset ID on success.

#### export-preset

Export a preset to a specified path.

```bash
beebium-model-b export-preset <id> --output <filepath>
```

Copies the preset file to the specified location. Works with both system and user presets.

### AUN Map Subcommands

These subcommands edit the per-user `aun-map.json` (the standing AUN peer map; see [networking.md](networking.md#the-aun-map-file-aun-mapjson)). They are model-independent and need no ROMs or running server. They write the file atomically through the same library the running server uses, preserving entry order and unknown keys, so a running instance picks a write up on its next poll of the file. (The poll rides on the mDNS browse sweep, so an instance launched with `discovery=announce` or `discovery=off` does not see the change until `AunService.ReloadMap`, the sidebar's Reload, or a restart.) Every one honours the `BEEBIUM_AUN_MAP_FILEPATH` environment variable and a `--map-file <path>` (or `--map-file=<path>`) option, which wins over the environment; without either, the shared per-user path is used: `~/Library/Application Support/Beebium/aun-map.json` on macOS, `%APPDATA%\Beebium\aun-map.json` on Windows, `$XDG_CONFIG_HOME/beebium/aun-map.json` (else `~/.config/beebium/aun-map.json`) elsewhere. Unlike `--aun map-file=`, the word `none` is not special here. GUIs that talk to a running (possibly remote) server use the `AunService` map RPCs instead; these subcommands are for scripts and hand setup.

Exit codes: `64` for a bad argument (a malformed `net.stn`, net, port or subnet, or the wrong number of arguments), `65` when the existing file is malformed (the error names the position), `74` when the file cannot be written, `78` from `create-aun-map` when the file already exists.

#### report-aun-map-filepath

```bash
beebium-model-b report-aun-map-filepath [--map-file <path>]
```

Outputs the resolved `aun-map.json` path (after `--map-file` and `BEEBIUM_AUN_MAP_FILEPATH`).

#### create-aun-map

```bash
beebium-model-b create-aun-map [--map-file <path>]
```

Writes a template `aun-map.json` with one example peer (`0.254` at `192.168.1.10:32768`) and one example subnet (net `128` = `192.168.1.0/24`), whose labels explain the PiEconetBridge and RISC OS cases. Refuses to overwrite an existing file (exits `78`, configuration error).

#### show-aun-map

```bash
beebium-model-b [--format pretty|tsv|jsonl] show-aun-map [--map-file <path>]
```

Shows the parsed peers and subnets with their labels and host-resolution state, in the global output format. `pretty` lists the file path, then peers and subnets, and any unknown top-level keys (which a write always preserves). `tsv` has the columns `kind net stn host_or_subnet port label resolved` (`resolved` is the IPv4 address, or `unresolved`). `jsonl` emits one object per entry: `{"kind":"peer","net","station","host","port","label","resolved","resolved_ip"}` or `{"kind":"subnet","net","subnet","label"}`. A missing file shows as empty.

#### add-aun-peer

```bash
beebium-model-b add-aun-peer <net.stn> <host> <port> [--label <text>] [--map-file <path>]
```

Adds or replaces the `peers` entry for `net.stn` (net 0-255, station 1-254; both parts are required, e.g. `0.254`). `host` is an IPv4 literal or a DNS name; `port` is 1-65535. Creates the file if it does not exist. Entry order and unknown keys in the file are preserved.

#### remove-aun-peer

```bash
beebium-model-b remove-aun-peer <net.stn> [--map-file <path>]
```

Removes the `peers` entry for `net.stn`. Exits `0` whether or not an entry was present, reporting which.

#### add-aun-subnet

```bash
beebium-model-b add-aun-subnet <net> <a.b.c.0/24> [--label <text>] [--map-file <path>]
```

Adds or replaces the `subnets` entry for `net` (0-255; the RISC OS convention: station is the last octet, port 32768). Only `/24` is supported; the address is stored as written and its last octet is ignored.

#### remove-aun-subnet

```bash
beebium-model-b remove-aun-subnet <net> [--map-file <path>]
```

Removes the `subnets` entry for `net`. Exits `0` whether or not an entry was present, reporting which.

### capture-screenshot

Run the emulator headlessly for a short time and write the framebuffer to a PNG
(used to generate preset thumbnails). Accepts all `start` options (`--preset`,
`--fdc`, `--sideways`, `--tube-65c02`, ...), so it runs any coprocessor the
configuration installs, exactly as `start` would.

```bash
beebium-model-b capture-screenshot --output <filepath> [options]
```

| Option | Description |
|--------|-------------|
| `--output <filepath>` | Output PNG filepath (required) |
| `--duration <seconds>` | Emulation time before capture (default: the preset's `thumbnail_capture_delay_seconds`, else 2.0) |
| `--border <pixels>` | Black border around the image (default: 20) |
| `--crop <mode>` | `none` (default) or `auto` |

`--crop auto` crops to the screen's content -- a boot banner typically sits
top-left in a mostly black frame -- and enlarges it to the same image size and
border a `--crop none` thumbnail would have, so a thumbnail set stays uniform in
aspect and pixel size while the banner is legible. The crop keeps the frame's
aspect ratio and is resampled to the output with a filtered (Lanczos-3) per-axis
scale -- not pixel-replicated -- so the enlargement is smooth rather than blocky.
`--crop none` (and omitting `--crop`) leaves the output exactly as before,
scaled nearest-neighbour. An unknown mode is a usage error.

### help

Show help for a subcommand.

```bash
beebium-model-b help                # Global help
beebium-model-b help start          # Start subcommand help
beebium-model-b help list-fdcs      # List-fdcs subcommand help
```

## Exit Codes

Exit codes follow sysexits.h conventions:

| Code | Name | Description |
|------|------|-------------|
| 0 | OK | Success |
| 64 | USAGE | Command line usage error |
| 65 | DATAERR | Data format error |
| 66 | NOINPUT | Cannot open input file |
| 70 | SOFTWARE | Internal software error |
| 74 | IOERR | I/O error |
| 78 | CONFIG | Configuration error |

## Examples

```bash
# Basic usage with defaults
beebium-model-b
beebium-model-b start

# Load a game disc and auto-boot
beebium-model-b start --floppy 0:elite.ssd --auto-boot

# Replace BASIC with Forth
beebium-model-b start --sideways slot=15:type=rom:image=forth.rom

# Multiple ROMs
beebium-model-b start --sideways slot=14:type=rom:image=dfs.rom --sideways slot=13:type=rom:image=viewsheet.rom

# Configure slot as RAM
beebium-model-b start --sideways slot=4:type=ram

# Sideways RAM, write-protected from boot (ATPL Sidewise slot 15)
beebium-model-b-atpl-sidewise start --sideways slot=15:type=ram:write-protect

# Remove default DFS on Model B+ (leave slot 11 empty)
beebium-model-b-plus start --sideways slot=11:type=empty

# Dynamic port for testing
beebium-model-b start --port 0

# Use default port in hex (0xBEEB = 48875)
beebium-model-b start --port 0xbeeb

# Wait for API control (automated testing)
beebium-model-b start --wait=api --port 0

# Start in Mode 0 instead of Mode 7
beebium-model-b start --screen-mode 0

# Screen mode in binary (0b101 = 5)
beebium-model-b start --screen-mode 0b101

# Keyboard links in hex (0xff = 255)
beebium-model-b start --links 0xff

# List available disc controllers
beebium-model-b list-fdcs

# Get machine info
beebium-model-b describe-machine

# Output formats (auto-detected by default)
beebium-model-b --format pretty list-fdcs    # Human-friendly
beebium-model-b --format tsv describe-machine # Tab-separated
beebium-model-b --format jsonl list-fdcs     # JSON Lines
beebium-model-b list-fdcs | cat               # Auto-selects tsv (piped)

# Install disc controller (Model B only)
beebium-model-b start --fdc acorn-1770

# Help variants
beebium-model-b --help              # Global help
beebium-model-b help                # Global help
beebium-model-b help start          # Start subcommand help
beebium-model-b start --help        # Start subcommand help
```

## Environment Variables

| Variable | Description |
|----------|-------------|
| `BEEBIUM_NO_PACING` | Disable real-time pacing (run at maximum speed) |
| `BEEBIUM_AUN_MAP_FILEPATH` | The AUN map file, used when `--aun map-file=` / `--map-file` is not given (empty = unset) |
| `BEEBIUM_AUN_AUTO_STATE_FILEPATH` | The per-host `--station auto` hint file (default `aun-auto-next` in the per-user directory that holds the default map file); `none` disables the hint |
| `BEEBIUM_AUN_AUTO_MIN_OBSERVE_MS`, `BEEBIUM_AUN_AUTO_QUIET_MS`, `BEEBIUM_AUN_AUTO_BUDGET_MS` | `--station auto` browse timings (defaults 1500 / 600 / 4000) |
| `BEEBIUM_AUN_TRACE` | Set (to anything) to log AUN packets and discovery collisions to stderr |
| `BEEBIUM_PICONET_TRACE` | Set (to anything) to log the Piconet serial protocol to stderr |

## Server Lifecycle

1. **Startup**: Server loads ROMs, initializes machine, binds gRPC port
2. **Ready**: Prints "BBC Model B ready. Press Ctrl+C to stop."
3. **Wait** (if `--wait`): Blocks until condition met
4. **Running**: Main emulation loop at ~2MHz (paced) or maximum speed
5. **Shutdown**: On SIGINT/SIGTERM, notifies clients via `WatchServerStatus`, then exits

Clients can subscribe to `SystemService/WatchServerStatus` to receive:
- `SERVER_STATUS_READY` immediately on subscription
- `SERVER_STATUS_SHUTTING_DOWN` when shutdown begins (with grace period)

## See Also

- [grpc-server.md](grpc-server.md) - gRPC service documentation
- [deployment.md](deployment.md) - ROM discovery and installation
- [keyboard.md](keyboard.md) - Keyboard matrix documentation
