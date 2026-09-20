# Sideways ROM/RAM Slots

Beebium models the BBC Micro family's paged-memory area (`&8000`-`&BFFF`,
selected by ROMSEL at `&FE30`) as a fixed slot topology that varies by
machine variant. The topology dictates which logical slot numbers exist,
which physical sockets they map to, what types each socket can hold
(ROM/RAM/empty), and whether the type can change at runtime. Every CLI
validation, gRPC response, and front-end UI decision about sideways
memory is driven from this single source of truth.

This document covers:

- the slot/socket layout for each supported machine variant;
- the rules that startup configuration is validated against;
- the motherboard-link mechanism (currently Model B+ S13);
- how the topology is exposed through `SidewaysService.GetSlotStatus`;
- known links not yet modelled, with pointers to manuals.

## Concepts

### Logical slot vs physical socket

A **logical slot** is a four-bit ROMSEL value (0-15) the BBC writes to
`&FE30` to select which paged ROM the CPU sees at `&8000`-`&BFFF`. A
**physical socket** is the EPROM socket on the motherboard that the
slot's reads and writes are routed to. The relationship depends on the
PCB:

- **Model B** (`AliasedBankedMemory`): partial address decoding (only
  two ROMSEL bits reach the decoder) means each of the four sockets
  responds to *four* slot numbers. IC52 answers slots 0/4/8/12, IC88
  answers 1/5/9/13, IC100 answers 2/6/10/14, IC101 answers 3/7/11/15.
  This is why MOS finds BASIC at slot 15 even though there are only
  four chips installed - slot 15 aliases to the same socket as slot 3.

- **Model B+** (`BankedMemory<...>`): six independent ROM sockets, each
  wired to a fixed pair of slots: IC35 = 2/3, IC44 = 4/5, IC57 = 6/7,
  IC62 = 8/9, IC68 = 10/11, IC71 = 14/15 (or 0/1, see S13 below). No
  socket is wired to slots 12 or 13 on a stock B+; reads from those
  slots return open bus.

- **Model B with ROM/RAM expansion board** (`ConfigurableBankedMemory`):
  full 4-bit ROMSEL decoding, sixteen independent slots, each its own
  socket. Beebium does not model a specific real-world board; this is
  a notional "sufficiently sophisticated" board with hot-reconfigurable
  slot types - useful as a flexible test bed.

- **Model B with ATPL Sidewise** (`ConfigurableBankedMemory`): the
  historical ATPL Sidewise board, full 4-bit ROMSEL decoding, sixteen
  slots. Slots 0-14 are ROM sockets; slot 15 is the board's single
  RAM/ROM socket (the real 15a/15b halves modelled as one 16K slot).
  Slot 15 has two distinguishing behaviours: **write-through** (any write
  to `&8000-&BFFF` is latched into the slot-15 RAM regardless of the
  currently paged ROM, so the RAM only reads back when slot 15 is paged
  in) and a runtime **write-protect** switch. Chip population is fixed at
  launch; the write-protect switch is the only runtime control. Intended
  to replace the notional ROM/RAM board.

### Socket capabilities

Each socket carries four boolean capability flags:

| Flag | Meaning |
|------|---------|
| `supports_rom` | A ROM image can be loaded into this socket. |
| `supports_ram` | The socket can be configured as sideways RAM. |
| `supports_empty` | The socket can be left vacant. |
| `runtime_configurable` | The socket *type* (ROM/RAM/empty) can be changed at runtime via `SidewaysService.ConfigureSlot`, without restarting the server. |

**Every present socket supports the ROM/RAM/empty trifecta.** Third-party
sideways-RAM modules that plugged into a ROM socket (with a flying lead for
the R/W line) were commonplace, so Beebium lets any socket hold a ROM, be
sideways RAM, or be left vacant rather than encoding per-machine
restrictions that buy little accuracy and would force a separate mechanism
for configuring RAM in those sockets. This is the `SocketSpec` default; a
machine only sets a capability `false` for a socket that genuinely cannot
be it. (We favour this flexibility over strict historical accuracy.)

`runtime_configurable` is an orthogonal axis. Real chip sockets are not
runtime-reconfigurable: changing the type means powering off, swapping a
chip, and powering back on. The flag reflects emulator policy, not hardware
capability:

| Variant | `runtime_configurable` |
|---------|------------------------|
| Model B | `false` (real-hardware-faithful) |
| Model B+ | `false` (real-hardware-faithful) |
| Model B with ROM/RAM board | `true` (fantasy hot-swap test bed) |
| Model B with ATPL Sidewise | `false` (real-hardware-faithful) |

Future Master 128 cartridge slots will be `runtime_configurable=true`
to model cartridge insertion/removal during a session - the closest
real-hardware analogue to runtime reconfiguration.

### Motherboard links

Some BBC Micro variants have physical jumpers on the motherboard that
change the slot-to-socket wiring. The user picks a position once, when
configuring the server, via the `--motherboard-link KEY=VALUE` CLI
option (see [cli.md](cli.md#motherboard-links)). The link state is
not runtime-mutable; servers report their configured link state to
clients via `GetSlotStatus`.

The only link currently modelled is **Model B+ S13**:

- `s13=south` (factory default) - IC71 answers slots 14 and 15. Slots
  0 and 1 are not wired to any socket and read as open bus.
- `s13=north` - IC71 answers slots 0 and 1. Slots 14 and 15 are not
  wired and read as open bus.

The compass directions match the Model B+ Service Manual (sec. 5.4.1).

This is the link selecting where the standard 16K BASIC ROM appears.
With S13=North, slot 15 is freed for a different language ROM, with
BASIC paged in at slot 0/1.

## Per-machine reference

### Model B

```
| Socket | IC    | Slots         | Default content    |
|--------|-------|---------------|--------------------|
|   0    | IC52  | 0, 4, 8, 12   | (empty)            |
|   1    | IC88  | 1, 5, 9, 13   | DFS (when fitted)  |
|   2    | IC100 | 2, 6, 10, 14  | (empty)            |
|   3    | IC101 | 3, 7, 11, 15  | BASIC              |
```

All four sockets support ROM/RAM/empty at startup. None are
runtime-reconfigurable.

### Model B+

```
| Socket | Slots               | Default content | Capabilities      |
|--------|---------------------|-----------------|-------------------|
| IC35   | 2, 3                | (empty)         | ROM / RAM / empty |
| IC44   | 4, 5                | (empty)         | ROM / RAM / empty |
| IC57   | 6, 7                | (empty)         | ROM / RAM / empty |
| IC62   | 8, 9                | (empty)         | ROM / RAM / empty |
| IC68   | 10, 11              | DFS             | ROM / RAM / empty |
| IC71   | 14, 15 / 0, 1 (S13) | BASIC           | ROM (fixed)       |
```

IC71's slot pair is selected by link S13; the inactive pair is
electrically dead. Slots 12 and 13 do not exist on a stock B+.

The five user ROM sockets (IC35..IC68) accept ROM, third-party
sideways RAM, or an empty socket - so the integral DFS at IC68 can be
removed or replaced and any user socket can be configured as RAM. IC71
is the soldered MOS+BASIC system ROM and cannot be reconfigured: it
reports `supports_rom=true, supports_ram=false, supports_empty=false`,
so `--sideways 14:ram` or `--sideways 15:empty` are rejected at
startup. None of the B+ sockets is runtime-reconfigurable.

The B+ 64K has no built-in sideways RAM - that arrives with the B+
128K, documented separately below.

Each B+ ROM socket also has a **device-size link** (S9 IC35, S11 IC44,
S12 IC57, S15 IC62, S18 IC68, S19 IC71) that selects 16K mode (West,
default) or 32K mode (East). In 16K mode, ROMSEL bit 0 is not routed
to the chip's A14, so the 16K image is aliased across both slots of
the pair - this is what our emulator models for every user socket.
In 32K mode, the two slots of the pair address different halves of
the 32K device; we don't model this today. IC71 is hardwired East
because the stock B+ system ROM is a 32K MOS+BASIC combo, with the
MOS in the high 16K (at &C000-&FFFF, not sideways) and BASIC in the
low 16K (sideways, aliased across the S13-selected pair).

### Model B+ 128K

```
| Socket | Slots                       | Default content | Capabilities      |
|--------|-----------------------------|-----------------|-------------------|
| IC35   | 2, 3                        | (empty)         | ROM / RAM / empty |
| IC44   | 4, 5                        | (empty)         | ROM / RAM / empty |
| IC57   | 6, 7                        | (empty)         | ROM / RAM / empty |
| IC62   | 8, 9                        | (empty)         | ROM / RAM / empty |
| IC68   | 10, 11                      | DFS             | ROM / RAM / empty |
| IC71   | 14, 15 / 0, 1 (S13)         | BASIC           | ROM (fixed)       |
| SRAM W | 12                          | (RAM, blank)    | RAM (fixed)       |
| SRAM X | 13                          | (RAM, blank)    | RAM (fixed)       |
| SRAM Y | 0 / 14 (opposite IC71, S13) | (RAM, blank)    | RAM (fixed)       |
| SRAM Z | 1 / 15 (opposite IC71, S13) | (RAM, blank)    | RAM (fixed)       |
```

The B+ 128K adds 64K of soldered sideways RAM split into four 16K banks
labelled W/X/Y/Z (Acorn AN 030). W and X are wired permanently to slots
12 and 13. Y and Z share the same two slot numbers as IC71 (BASIC) and
swap pairs based on link S13:

- **S13=South** (factory default): BASIC at 14/15, SRAM Y/Z at 0/1.
- **S13=North**: BASIC at 0/1, SRAM Y/Z at 14/15.

The five user sockets (IC35..IC68) behave exactly as on the B+ 64K.
IC71 is again ROM-only. The four SRAM banks report
`supports_rom=false, supports_ram=true, supports_empty=false` - they
are soldered RAM, so `--sideways 12:rom:foo.rom` is rejected at
startup; pre-loading a RAM image with `--sideways 12:ram:foo.bin` is
supported. None of the B+ 128K sockets is runtime-reconfigurable.

### Model B with ROM/RAM expansion board

Sixteen independent slots, each a single-slot socket of the same name
(`Slot 0` through `Slot 15`). Each supports ROM/RAM/empty and is
runtime-reconfigurable. No aliasing.

### Model B with ATPL Sidewise

Sixteen independent slots, no aliasing. Slots 0-3 are the relocated
motherboard sockets (`Motherboard ROM 0-3`) and slots 4-14 are the
board's ROM sockets (`Sidewise ROM 4-14`): all `supports_rom=true,
supports_ram=false`. Slot 15 (`Sidewise RAM/ROM 15`) is the board's only
RAM-capable socket (`supports_rom=true, supports_ram=true`). No socket is
runtime-reconfigurable: chip population, and whether slot 15 holds RAM or
a ROM, is fixed at launch with `--sideways`.

Slot 15 has two board-specific behaviours when fitted with RAM:

- **Write-through.** A CPU write to `&8000-&BFFF` is latched into the
  slot-15 RAM directly, irrespective of and without affecting the
  currently paged ROM. Reads stay ROMSEL-gated, so the RAM only reads
  back when slot 15 is paged in via `&FE30`.
- **Write-protect.** A runtime switch (the board's S6 link) inhibits
  writes to the slot-15 RAM. Toggle it with
  `SidewaysService.SetSlotWriteProtect`; the current state is reported in
  `SocketStatus.write_protected`.

BASIC defaults to slot 14 (the manual reserves slot 15 for RAM). Battery
backup is not modelled.

## Startup validation

When the server parses `--sideways` arguments, it groups them by the
physical socket they target and rejects any of:

- a slot that does not exist on the configured topology
  (e.g. `--sideways 12:rom:foo.rom` on a Model B+ 64K);
- a type the socket does not support (`--sideways 14:ram` on a B+ -
  IC71 is ROM-only; `--sideways 12:rom:foo.rom` on a B+ 128K - SRAM W
  is RAM-only; etc.);
- two requests targeting the same socket with different types
  (e.g. `--sideways 0:rom:foo.rom --sideways 4:ram` on a Model B - both
  reach IC52);
- two requests targeting the same socket with different ROM image
  paths (a single chip can only hold one image);
- the same slot specified twice.

Errors are reported in a single multi-line message naming the affected
socket(s) and full alias set, so multiple problems can be fixed in one
edit. Example:

```
Error: Invalid --sideways configuration:
  * Socket IC52 (aliased to slots 0, 4, 8, 12) has conflicting type
    requests:
      - --sideways 0:ROM:bbc-basic_2.rom
      - --sideways 12:RAM
    A single physical socket cannot hold more than one type.
```

The default-language and default-DFS ROMs are skipped automatically
when the user's `--sideways` already targets the same socket (so a
user `--sideways 0:rom:bas128.rom` under B+ S13=North is not silently
overwritten by the default BASIC at slot 15), and when the default
slot does not exist on the configured topology.

## Runtime reconfiguration

`SidewaysService.ConfigureSlot` mirrors the topology rules:

- the request is rejected if the slot does not exist;
- the request is rejected if the socket is not runtime-reconfigurable
  (the error names the socket label and points the user at
  `--sideways`);
- otherwise the live device is updated.

Clients should consult `SocketStatus.capabilities.runtime_configurable`
to decide whether to expose a "Convert to RAM" / "Replace ROM" UI
control for a given socket - graying it out on Model B and Model B+
saves the user a round-trip and a confusing error.

## gRPC API

`beebium.SidewaysService` (`src/service/proto/sideways.proto`) exposes:

- **`GetSlotStatus`** returns the live topology and link state:

  ```proto
  message GetSlotStatusResponse {
      bool has_aliasing = 1;
      uint32 num_physical_slots = 2;
      repeated SocketStatus sockets = 3;
      uint32 selected_bank = 4;
      repeated MotherboardLink motherboard_links = 5;
  }

  message SocketStatus {
      uint32 socket_index = 1;
      repeated uint32 aliased_slots = 2;
      SidewaysSlotType type = 3;
      bool populated = 4;
      string image_name = 5;
      string socket_label = 6;
      SocketCapabilities capabilities = 7;
      RomHeader rom_header = 8;
      bool write_protected = 9;
  }
  ```

- **`ConfigureSlot`** changes a runtime-configurable socket's type
  and/or loads an image. Subject to the rules above.

- **`SetSlotWriteProtect`** engages or releases a RAM slot's
  write-protect switch (e.g. the ATPL Sidewise slot 15). Rejected for
  slots that are not currently RAM, and for machines with no
  write-protect control. The resulting state is also reported in
  `SocketStatus.write_protected`.

- **`ReadSlotData`** reads bytes from a slot for inspection.

- **`SubscribeEvents`** streams bank-selection and slot-configuration
  events.

Clients are expected to display sockets, not slots, when they need to
show the user a hardware-faithful view. The `aliased_slots` and
`socket_label` fields together let a UI render "Socket IC52 (slots 0,
4, 8, 12)" without baking machine-specific assumptions into client
code.

## Future work

The following links are documented in the BBC manuals and would slot
into the existing `MotherboardLinks` mechanism if and when they
become useful to model:

### Model B

Links **S20**, **S21**, **S22** control ROM address-space mapping (see
issue [#31](https://github.com/rob-smallshire/beebium/issues/31) for a
summary). They affect how ROMSEL bits and address bits are combined
to select a socket, and whether the MOS and sideways areas are swapped
at the top of the address map.

References:

- *A Hardware Guide for the BBC Microcomputer*, chapter 4
  ([rk.nvg.ntnu.no](http://rk.nvg.ntnu.no/bbc/doc/A%20Hardware%20Guide%20for%20the%20BBC%20Microcomputer/bbc_hw_04.htm)).
- *Advanced User Guide*, Appendix I (in `docs/manuals_text/`).

### Model B+

Every B+ ROM socket has a device-size link selecting 16K mode (W,
default - one image aliased across the slot pair) or 32K mode (E -
two distinct halves at the two slot numbers): **S9** IC35, **S11**
IC44, **S12** IC57, **S15** IC62, **S18** IC68, **S19** IC71. Per
the Model B+ Service Manual sec. 5.4.1.

S19 is hardwired East at the factory because IC71 holds the 32K
MOS+BASIC system ROM; the other five ship in W and our emulator
matches that. Modelling 32K mode for user sockets requires a per-
socket "16K vs 32K" enum in `MotherboardLinks` and topology entries
that split the socket into two distinct logical entries when 32K
mode is selected. Niche enough that it's left as future work.

Earlier comments here described S18/S19 as speed links - they aren't;
that was confusion with the Model B's links of the same number.

### Master 128

Cartridge slots map into sideways space and were swapped during a
session in practice. When Master 128 support lands, the cartridge
sockets will use `runtime_configurable=true` to model cartridge
insertion/removal through `ConfigureSlot`.
