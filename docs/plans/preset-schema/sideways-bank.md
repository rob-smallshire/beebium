# Sideways ROM/RAM Bank Schema

## Implementation status

Most of this design has landed (branch `preset-memory-tab`, off `master`).
What exists today:

- **Preset `sideways_bank` section** is parsed by `parse_sideways_section`
  in `PresetLoader.hpp` into `PresetConfig::sideways`. Shape:
  `{ "sideways_bank": { "slots": [ { "slot": 14, "type": "rom",
  "image_uri": "acorn-dfs_2_26.rom" } ] } }`. A slot's `type` is
  `rom`/`ram`/`empty`; a `ram` slot may also carry `"write_protected": true`
  to engage its write-protect switch at boot (only where the socket has one);
  `image_uri` is a ROM-library name or path, stored
  **verbatim** and resolved via the ROM search path (like `--sideways` and
  the machine defaults), not rewritten to a `file://` URI the way disc
  images are.
- **CLI overrides preset, per slot.** `apply_preset` stages preset slots;
  `merge_preset_sideways_configs` (end of `parse_start_arguments`) folds
  them into the active config only where a CLI `--sideways` hasn't claimed
  that slot, addressing each socket at the slot its content occupies to
  avoid aliased-socket conflicts.
- **`describe-preset-schema`** emits a `sideways_bank` section built from
  the machine's `SlotTopology`: per-socket `label`, wired `slots`,
  `capabilities`, `runtime_configurable`, plus `has_aliasing` and the
  machine's `default_roms`. (Every socket now supports ROM/RAM/empty - see
  [sideways-slots.md](../../sideways-slots.md).)
- **`create-preset`** gained `--fdc`, `--sideways`, and `--release-date`,
  so the build generates the shipped presets through the same code path
  users create their own with (CMake `add_system_preset`). Shipped set:
  `model-b`, `model-b-disc`, `model-b-romram-disc`, `model-b-plus`.
- **ROM titles and kinds.** `SidewaysRomHeader`
  (`src/core/include/beebium/`) parses the standard ROM header (this
  document's sibling, `sidewrom.pdf`) and, via `RomFsDetection.hpp`, also
  detects whether a service ROM carries ROM Filing System data (see
  [romfs-detection.md](../../romfs-detection.md)). The `describe-rom`
  subcommand exposes both as JSON, including a convenience `kinds` array
  (any combination of `"language"`, `"service"`, `"romfs"`). The macOS New
  Machine **Memory** tab is a socket-oriented configurator (highest
  priority first) that shows real ROM titles/versions ("Acorn DFS 2.26")
  instead of filenames and appends the kinds to each socket's caption
  ("... · language · service · ROMFS" for an Acornsoft cartridge), with a
  tri-state ROM/RAM/Empty control and a Browse/Clear/Copy Path/Reveal
  menu, applying changes at launch via `--sideways`.

Resolved design questions:

- **Slot priority** derives from the slot number (highest slot wins the
  language selection at reset); the UI orders sockets by it. Presets do not
  set explicit priority.

Still deferred:

- **Known-ROM catalogue / `known_roms` / categories** in the schema - the
  Memory tab uses a file picker plus parsed titles instead.
- **Live-machine "Memory" sidebar** for a running core (read-only display
  via `SidewaysService.GetSlotStatus`) - runtime mutation of sideways
  contents is intentionally out of scope.
- **Save-as-preset** persistence of Memory/Storage edits in the New Machine
  dialog (a TODO shared with the Storage tab; `create-preset` already
  accepts the flags).

The sections below are the original design notes; treat them as background
where they go beyond the above.

## Domain Concept

The BBC Micro has 16 "sideways" ROM sockets (slots 0-15) that share a 16KB address space ($8000-$BFFF). Only one slot is active at a time, selected by writing to $FE30. This enables multiple ROMs (languages, filing systems, utilities) to coexist.

Some machines have sideways RAM in certain slots, allowing software loading and battery-backed storage.

## Hardware Reality

### Model B
- 4 physical ROM sockets (typically slots 12-15)
- Slot 15: Language ROM (BASIC by default)
- Slots 12-14: Available for DFS, utilities
- Lower slots need expansion hardware

### Model B+
- 8 ROM sockets
- 2 sideways RAM slots (software-selectable)

### Model B with ROM/RAM Board
- 16 slots, each independently configurable as ROM or RAM
- Common for development and multi-ROM setups

### Master 128
- 8 ROM sockets
- 4 sideways RAM slots
- Cartridge slots add more

## Schema Design

```json
{
  "type": "sideways_bank",
  "slot_count": 16,
  "physical_sockets": [12, 13, 14, 15],
  "slots": [
    {
      "slot": 15,
      "capabilities": ["rom"],
      "default_type": "rom",
      "default_image": "bbc-basic_2.rom",
      "role": "language",
      "label": "Language ROM"
    },
    {
      "slot": 14,
      "capabilities": ["rom"],
      "default_type": "empty"
    },
    {
      "slot": 4,
      "capabilities": ["rom", "ram"],
      "default_type": "empty",
      "note": "Expansion board required"
    }
  ],
  "known_roms": [
    {
      "id": "bbc-basic_2.rom",
      "label": "BBC BASIC II",
      "category": "language",
      "priority": 127
    },
    {
      "id": "acorn-dfs_0_90.rom",
      "label": "Acorn DFS 0.90",
      "category": "filing",
      "priority": 64
    },
    {
      "id": "watford-dfs_1_44.rom",
      "label": "Watford DFS 1.44",
      "category": "filing",
      "priority": 64
    }
  ],
  "categories": [
    {"id": "language", "label": "Language ROMs"},
    {"id": "filing", "label": "Filing Systems"},
    {"id": "utility", "label": "Utilities"},
    {"id": "application", "label": "Applications"}
  ]
}
```

## Configuration Values

Presets use a `slots` array within the `sideways_bank` namespace:
```json
{
  "sideways_bank": {
    "slots": [
      { "slot": 15, "type": "rom", "image_uri": "library://roms/bbc-basic_2.rom" },
      { "slot": 14, "type": "rom", "image_uri": "library://roms/acorn-dfs_0_90.rom" },
      { "slot": 13, "type": "empty" },
      { "slot": 4, "type": "ram", "image_uri": "file:///path/to/preload.bin" }
    ]
  }
}
```

Only non-default slots need to be specified. Unspecified slots retain their default configuration.

## CLI Mapping

```
--sideways slot=15:type=rom:image=library://roms/bbc-basic_2.rom
--sideways slot=14:type=rom:image=library://roms/acorn-dfs_0_90.rom
--sideways slot=13:type=empty
--sideways slot=4:type=ram:image=file:///path/to/preload.bin
```

## UI Considerations

### Compact View
Show only non-default slots, with "Show all slots" expander.

### Detailed View
Table with columns: Slot | Type | Image | Priority | Notes

### Interactions
- Drag-and-drop ROM files onto slots
- Dropdown for known ROMs grouped by category
- "Custom..." option for arbitrary files
- Warning for slots outside physical sockets

### Validation
- Only one language ROM should be active (warn, don't prevent)
- Filing system ROM needed if disc controller present
- Priority conflicts (same priority in multiple slots)

## Open Questions

1. **Slot priority**: Should presets specify priority, or derive from slot number?

2. **RAM pre-loading**: RAM slots can be pre-loaded from a file. Is this common enough to warrant UI support, or just CLI?

3. **Battery-backed RAM**: Some setups preserve RAM contents. How to represent "save RAM on exit"?

4. **ROM libraries**: Should there be a managed library of known ROMs, or just filesystem paths?

5. **Compact representation**: For presets that only change one or two slots, storing all 16 feels verbose. Delta from defaults?

6. **Mutual exclusion**: Some ROMs conflict (multiple filing systems). Schema or frontend responsibility?

## Use Cases

### UC1: Minimal Gaming Setup

Just BASIC and DFS — the most common configuration.

```json
{
  "model": "model-b",
  "sideways_bank": {
    "slots": [
      { "slot": 15, "type": "rom", "image_uri": "library://roms/bbc-basic_2.rom" },
      { "slot": 14, "type": "rom", "image_uri": "library://roms/acorn-dfs_0_90.rom" }
    ]
  }
}
```

This is the default for Model B presets. Most games expect this.

### UC2: Professional Setup with Multiple Filing Systems

User needs both DFS and ADFS for different discs.

```json
{
  "model": "model-b",
  "sideways_bank": {
    "slots": [
      { "slot": 15, "type": "rom", "image_uri": "library://roms/bbc-basic_2.rom" },
      { "slot": 14, "type": "rom", "image_uri": "library://roms/acorn-dfs_0_90.rom" },
      { "slot": 13, "type": "rom", "image_uri": "library://roms/acorn-adfs_1_30.rom" }
    ]
  }
}
```

`*DISC` selects DFS, `*ADFS` selects ADFS.

### UC3: Development Environment

BASIC, DFS, plus VIEW word processor and a toolkit ROM.

```json
{
  "model": "model-b",
  "sideways_bank": {
    "slots": [
      { "slot": 15, "type": "rom", "image_uri": "library://roms/bbc-basic_2.rom" },
      { "slot": 14, "type": "rom", "image_uri": "library://roms/acorn-dfs_0_90.rom" },
      { "slot": 13, "type": "rom", "image_uri": "library://roms/view-wordprocessor.rom" },
      { "slot": 12, "type": "rom", "image_uri": "library://roms/beebug-toolkit.rom" }
    ]
  }
}
```

### UC4: ROM/RAM Board for Software Development

All 16 slots available, with sideways RAM for loading test ROMs.

```json
{
  "model": "model-b-romram",
  "sideways_bank": {
    "slots": [
      { "slot": 15, "type": "rom", "image_uri": "library://roms/bbc-basic_2.rom" },
      { "slot": 14, "type": "rom", "image_uri": "library://roms/acorn-dfs_0_90.rom" },
      { "slot": 4, "type": "ram", "image_uri": null },
      { "slot": 5, "type": "ram", "image_uri": null }
    ]
  }
}
```

RAM slots start empty; software can load ROMs into them with `*SRLOAD`.

### UC5: Pre-loaded Sideways RAM

RAM slot pre-loaded with a ROM image for testing.

```json
{
  "model": "model-b-romram",
  "sideways_bank": {
    "slots": [
      { "slot": 4, "type": "ram", "image_uri": "file:///path/to/my-rom-under-test.rom" }
    ]
  }
}
```

Useful for ROM developers who want to test without burning EPROMs.

### UC6: Cassette-Only (No DFS)

Original Model B with just BASIC — no disc system.

```json
{
  "model": "model-b",
  "sideways_bank": {
    "slots": [
      { "slot": 15, "type": "rom", "image_uri": "library://roms/bbc-basic_2.rom" },
      { "slot": 14, "type": "empty" }
    ]
  }
}
```

Represents the machine as shipped before disc upgrades became standard.

### UC7: Multiple BASIC Versions

Testing software compatibility with different BASIC versions.

```json
{
  "model": "model-b",
  "sideways_bank": {
    "slots": [
      { "slot": 15, "type": "rom", "image_uri": "library://roms/bbc-basic_2.rom" },
      { "slot": 12, "type": "rom", "image_uri": "library://roms/bbc-basic_1.rom" }
    ]
  }
}
```

`*BASIC` enters slot 15 (BASIC II), `*FX142,12` enters slot 12 (BASIC I).

### UC8: Hi-BASIC for More Memory

Hi-BASIC relocates PAGE to give more program space.

```json
{
  "model": "model-b",
  "sideways_bank": {
    "slots": [
      { "slot": 15, "type": "rom", "image_uri": "library://roms/hi-basic.rom" },
      { "slot": 14, "type": "rom", "image_uri": "library://roms/acorn-dfs_0_90.rom" }
    ]
  }
}
```

Gives about 4KB more space for BASIC programs.

---

## Priority and ROM Selection

The BBC selects the highest-priority ROM with a language entry point at boot. Slot number acts as a secondary sort.

| ROM | Typical Priority | Behaviour |
|-----|-----------------|-----------|
| BASIC | 127 | Highest; enters by default |
| COMAL | 100 | Alternative language |
| DFS | 64 | Filing system; no language entry |
| ADFS | 64 | Filing system |
| Utilities | 0-63 | Lower; service ROMs only |

**Schema question**: Should presets allow overriding priority, or always use the ROM's built-in priority?
