# ATPL Sidewise Machine Variant

## Overview

A new machine variant, `model-b-atpl-sidewise`, that emulates a BBC Model B
fitted with the **ATPL Sidewise** sideways ROM/RAM expansion board. It is built
alongside the existing `model-b-romram` and is intended to eventually replace it.

The motivation (GitHub #94, addressing #72) is to move away from the "fantasy"
ROM/RAM board embodied by `model-b-romram` — a board with no historical
counterpart whose slots can be retyped RAM/ROM at will at runtime — towards
emulating *specific historical boards*. Otherwise it is not emulation.

Source material for the board is in `docs/atpl_sidewise/`:

- `ATPL-Sidewise-User-Manual.txt` — OCR'd and hand-corrected transcription of
  the 1983 ATPL Sidewise User Manual.
- `ATPL-Sidewise-User-Manual.pdf` — the original 12-page scan.
- `ATPLSidewise.pdf` — a reverse-engineered KiCad schematic.

## Scope

This is a *deliberately simplified* model of the board — faithful where it
matters for software compatibility, simplified where the physical detail has no
meaning in emulation.

**Modelled:**

- 16 sideways slots. Slots 0–14 are ROM sockets; slot 15 is a single RAM-or-ROM
  socket (the real board splits it into 8K halves 15a/15b — we do not).
- Slot 15 **write-through**: any CPU write to `&8000–&BFFF` lands in slot-15 RAM
  directly, irrespective of and without affecting the currently paged ROM. RAM
  reads back only when slot 15 is paged in via the `&FE30` ROMSEL latch. This is
  the defining Sidewise behaviour and is relied on by software such as the ROM
  Filing System and ATPL's own SPOOLIT printer buffer.
- **Write-protect** on slot 15 — a runtime control emulating the S6 link, which
  users commonly wired to a rear-panel toggle switch.

**Not modelled:**

- The 15a/15b split (single slot 15 instead).
- Battery backup — has no meaning in emulation. Sideways RAM contents persist
  across a soft reset (BREAK) as ordinary RAM and are lost on server exit.
- The physical link matrix (S1–S8) beyond the write-protect switch. The layout
  is fixed rather than link-configurable.

## Design principle: model runtime controls only where the hardware had one

The fantasy `model-b-romram` board lets a client retype any slot RAM/ROM at
runtime. A real board does not: changing which ROM is in a socket, or whether
slot 15 holds a RAM chip or an EPROM, required powering down and swapping a
chip. The one thing the Sidewise *could* change while running was the S6
write-protect switch.

So for this variant:

- **Launch-time only:** which ROM images populate slots 0–14, and whether slot 15
  is fitted as RAM or ROM. Set via CLI / preset, exactly like fitting chips.
- **Runtime only:** the slot-15 write-protect switch.

Concretely, `slot_topology()` marks **all 16 slots `runtime_configurable = false`**,
so the existing `SidewaysService.ConfigureSlot` RPC correctly refuses to
hot-reconfigure this board. Write-protect gets its own dedicated control (below).

## Architecture

### Base engine

Base the hardware policy on `ModelBRomRamBoardHardware`, which uses
`ConfigurableBankedMemory` (16 uniform `ConfigurableSlot`s whose RAM/ROM/Empty
type is set at configuration time). This is preferred over the B+ 128K
`BankedMemory` (compile-time typed bank pack) because slot 15's RAM-or-ROM
identity is a **launch choice**, which maps directly onto the existing
`configure_slot_as_ram` / `load_sideways_rom` startup calls. All slots are then
locked against runtime reconfiguration via the topology flag above.

### Slot-15 write-through overlay

The entire codebase routes writes to `&8000–&BFFF` through the ROMSEL-selected
bank (`ConfigurableBankedMemory::write` → `slots_[selected_bank_].write`). There
is no write-through anywhere. It is added as an overlay in the hardware policy's
`write()`, following the precedent set by the B+ ANDY private-RAM interception,
which likewise diverts a `&8000`-range write before the memory map:

```
write(addr, value):
    if 0x8000 <= addr <= 0xBFFF
       and slot 15 is RAM
       and not slot15_write_protected_:
        sideways.write_bank(15, addr - 0x8000, value)   # direct, ignores selected_bank_
        return
    memory_map_.write(addr, value)                        # normal ROMSEL-gated path
```

- Reads are untouched — they stay ROMSEL-gated, so slot-15 RAM only reads back
  when slot 15 is paged in.
- If slot 15 is fitted as ROM, the overlay is inert; writes fall through and are
  ignored by the ROM socket, as expected.
- `ConfigurableBankedMemory` likely needs a `write_bank(bank, offset, value)`
  helper (a direct-to-bank counterpart of the existing `peek_bank`) if one is not
  already exposed.

### Write-protect as a per-bank SidewaysService feature

Write-protect is added as a **general per-bank capability**, not a slot-15
special case, because future boards will also have write-protectable RAM. On
`model-b-atpl-sidewise` only slot 15 is RAM, so only slot 15 is write-protectable.

- `sideways.proto`: add `bool write_protected` to `SocketStatus`, and a new RPC
  `SetSlotWriteProtect(slot, write_protected)` returning updated status. Rejects
  the request for any slot that is not RAM.
- `SidewaysServiceImpl`: implement under `with_emulation_paused`, driven by a new
  duck-typed concept on the machine (e.g. `HasSlotWriteProtect`).
- Hardware policy: `slot15_write_protected_` state + `set_slot_write_protected` /
  `is_slot_write_protected` accessors; the flag is surfaced through `slot_info` /
  topology so `GetSlotStatus` reports it.

## Change list

Core:

- `src/core/include/beebium/ModelBAtplSidewiseHardware.hpp` — new policy, copied
  from `ModelBRomRamBoardHardware` with:
  - `MACHINE_TYPE = "model-b-atpl-sidewise"`, display name/description.
  - `DEFAULT_LANGUAGE_SLOT = 14` (the manual installs BASIC in socket 14, since
    slot 15 is reserved for RAM).
  - `write()` overlay for slot-15 write-through.
  - `slot15_write_protected_` state + accessors.
  - `slot_topology()` with all slots `runtime_configurable = false`; slot 15
    advertises `supports_rom` + `supports_ram`.
- `src/core/include/beebium/devices/ConfigurableBankedMemory.hpp` — add
  `write_bank()` if needed.
- `src/core/include/beebium/Machines.hpp` — add
  `using ModelBAtplSidewise = Machine<Nmos6502, ModelBAtplSidewiseHardware>;`.

Service:

- `src/service/proto/sideways.proto` — `write_protected` field + `SetSlotWriteProtect`.
- `src/service/include/beebium/service/SidewaysService.hpp` — implement it.
- Regenerate Python and TypeScript stubs; run `npx tsc --noEmit` in the TS
  client (per repo rules for proto edits). A proto change rebuilds all servers.

Server / build:

- `src/server/main_model_b_atpl_sidewise.cpp` — 3-line entry point.
- `src/server/CMakeLists.txt` — `add_beebium_server(beebium-model-b-atpl-sidewise ...)`,
  add to `BEEBIUM_SERVERS_DEPS` and `install(TARGETS ...)`, and an
  `add_system_preset` for `model-b-atpl-sidewise-disc` (`--fdc acorn-1770`,
  DFS ROM in a slot, BASIC in slot 14, slot 15 = RAM, write-protect off).

Docs:

- `docs/sideways-slots.md` — describe the variant, write-through, per-bank
  write-protect.
- `docs/cli.md` — add `beebium-model-b-atpl-sidewise` to the machine table.
- Preset schema docs under `docs/plans/preset-schema/`.

Clients (later phase):

- Python client coverage for the new machine and `SetSlotWriteProtect`.
- macOS frontend: a slot-15 write-protect control. Per the repo convention this
  is state-with-side-effects, so an Indicator + Button, not a Toggle.

## Tests (written first, per TDD)

- `tests/test_model_b_atpl_sidewise_hardware.cpp`:
  - write to `&8000–&BFFF` reaches slot-15 RAM when RAM + not protected, even
    when a different ROM (e.g. slot 14 BASIC) is paged in;
  - the same write is ignored when write-protected;
  - the same write is ignored when slot 15 is fitted as ROM;
  - reads are ROMSEL-gated (slot-15 RAM reads back only when slot 15 selected);
  - `DEFAULT_LANGUAGE_SLOT` is 14.
- `tests/test_grpc_sideways.cpp` additions: variant reports 16 slots, all
  `runtime_configurable = false`; `ConfigureSlot` is rejected; `SetSlotWriteProtect`
  toggles slot 15 and is rejected for non-RAM slots; `GetSlotStatus` reports
  `write_protected`.
- `tests/test_sideways_validation.cpp` additions mirroring the romram topology
  cases.
- Python integration test analogous to `integration_tests/sideways-srload/`,
  exercising write-through and write-protect against a running server.

## Relationship to `model-b-romram`

Built alongside; `model-b-romram` is untouched for now. Once `model-b-atpl-sidewise`
and any further historical boards cover the use cases, `model-b-romram` can be
retired in a later change. No deprecation in this phase.

## Open items for later phases

- Additional historical boards (which motivated making write-protect a general
  per-bank feature).
- macOS frontend control and Python/TS client ergonomics.
