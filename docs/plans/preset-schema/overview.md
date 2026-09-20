# Preset Schema Overview

## Purpose

The preset schema defines how cores describe their configurable options. Frontends render configuration UI from this schema without hardcoding BBC Micro hardware knowledge.

## Design Principles

1. **Describe domain concepts, not widgets** — express "what BBC machines have", not "what controls to render"

2. **Two-tier structure** — domain-specific sections for rich semantics, generic primitives as fallback

3. **Sections are optional and additive** — models report only what they support; unknown sections are preserved

4. **Values are simple, schema is rich** — flat key-value pairs for CLI mapping, rich structure for UI

5. **Forward compatible** — new section types don't break old presets or frontends

## Documents in This Directory

| Document | Domain Area |
|----------|-------------|
| [os-rom.md](os-rom.md) | Operating system ROM selection |
| [sideways-bank.md](sideways-bank.md) | Sideways ROM/RAM bank (16 slots) |
| [storage.md](storage.md) | Storage systems: cassette, floppy, hard disc |
| [coprocessor.md](coprocessor.md) | Tube interface and second processors |
| [networking.md](networking.md) | Econet and AUN networking |
| [startup-options.md](startup-options.md) | Boot-time configuration |
| [generic-options.md](generic-options.md) | Fallback primitive types |

## Schema Structure

```json
{
  "schema_version": 1,
  "model": {
    "id": "model-b",
    "name": "BBC Model B",
    "description": "The original BBC Microcomputer with 32KB RAM"
  },
  "sections": [
    { "type": "os_rom", ... },
    { "type": "sideways_bank", ... },
    { "type": "storage", ... },
    { "type": "coprocessor", ... },
    { "type": "networking", ... },
    { "type": "startup_options", ... },
    { "type": "generic", ... }
  ]
}
```

## CLI Value Mapping

All configuration values must map to CLI arguments:

```bash
beebium-model-b start \
  --mos mos120 \
  --sideways slot=15:type=rom:image=basic.rom \
  --fdc acorn-1770 \
  --floppy 0:/path/to/game.ssd
```

The CLI accepts both filesystem paths and URIs. Paths are converted to `file://` URIs internally; explicit URIs like `library://games/Elite.ssd` are passed through. This provides convenience for quick CLI use while supporting portable presets.

This constraint shapes value format decisions throughout.

---

## Section Types: Domain-Specific vs Generic

### Domain-Specific Sections

Domain-specific sections model BBC Micro hardware concepts directly. They provide:

- **Rich semantics**: The schema knows what a "sideways bank" is, enabling intelligent UI
- **Validation rules**: Disc images need filing system ROMs; coprocessors need Tube interface
- **Interdependencies**: Selecting a floppy controller might auto-suggest DFS ROM
- **Sensible defaults**: BASIC II in slot 15, MOS 1.20 for Model B

| Section | BBC Hardware Concept |
|---------|---------------------|
| `os_rom` | Machine Operating System firmware |
| `sideways_bank` | Sideways ROM/RAM slots (16 banks at $8000-$BFFF) |
| `storage` | Cassette, floppy disc, hard disc subsystems |
| `coprocessor` | Tube interface and second processors |
| `networking` | Econet interface and AUN bridge |
| `startup_options` | Keyboard links (boot mode, screen mode) |

### Generic Sections

Generic sections handle emulator-specific options that don't map to BBC hardware:

- Video output mode (RGB, composite, RF simulation)
- Audio settings (buffer size, volume)
- Performance options (turbo mode, cycle accuracy)
- Debug options (break on BRK, trace execution)

These use primitive types (boolean, integer, enum, file_path) and render with standard controls.

---

## Schema Lifecycle

### 1. Core Discovery

Frontend discovers core executables and invokes CLI:

```bash
beebium-model-b describe-preset-schema
```

Core outputs JSON schema describing its configuration options.

### 2. Preset Creation

User creates a preset by:
- Starting from defaults (schema provides these)
- Or duplicating an existing preset
- Or importing a preset file

### 3. Preset Storage

Presets stored as JSON in user's application support directory:

```
~/Library/Application Support/Beebium/presets/
├── user-presets/
│   ├── elite-gaming.json
│   └── development-setup.json
└── imported/
    └── shared-classroom.json
```

### 4. Launch

The server executable reads preset files directly:

```bash
beebium-model-b start --preset=/path/to/elite.preset.beebium
```

CLI options can override specific values from the preset:

```bash
# Load preset but use a different disc image
beebium-model-b start --preset=elite.preset.beebium --floppy 0:/tmp/patched-elite.ssd

# Load preset but disable auto-boot for debugging
beebium-model-b start --preset=elite.preset.beebium --no-auto-boot
```

**Precedence:** CLI options merge with preset values. This allows targeted overrides without re-specifying the entire configuration.

**File extension:** `.preset.beebium` identifies preset files (e.g., `elite.preset.beebium`).

**Preset search paths:** (future) The server could search standard locations:
1. Explicit path (if absolute or relative path provided)
2. Current directory
3. `~/.config/beebium/presets/` (Linux)
4. `~/Library/Application Support/Beebium/presets/` (macOS)

---

## Cross-Section Relationships

Some configuration choices are *typically* paired, but these are conventions, not requirements:

### Storage ↔ Sideways Bank

| Storage Config | Typical Pairing |
|----------------|-----------------|
| `fdc_socket: { id: "acorn-1770" }` | DFS or ADFS ROM in a sideways slot |
| SCSI interface on 1 MHz bus | ADFS ROM in a sideways slot |

However, it's entirely valid to have:
- An FDC with no filing system ROM (hardware present but unusable)
- A filing system ROM with no FDC (ROM present but nothing to control)
- Multiple filing systems (DFS + ADFS side-by-side, standard on Master 128)

### Coprocessor ↔ Sideways Bank

| Coprocessor | Typical Pairing |
|-------------|-----------------|
| 6502 Second Processor | Tube client ROM |
| Z80 Second Processor | CP/M boot ROM |

### Startup Options ↔ Storage

| Startup Config | Typical Pairing |
|----------------|-----------------|
| `auto_boot: true` | Bootable disc in drive 0 |

**Design Philosophy**: The schema does not enforce these relationships. Frontends may offer gentle reminders (e.g., "You have an FDC but no filing system ROM — did you mean to add one?") but should never prevent unusual configurations. Users may have legitimate reasons for incomplete setups, and enforcing "correctness" would require domain knowledge the frontend shouldn't need. In practice, curated preset libraries will provide known-working configurations, solving the problem organically.

---

## Use Cases

### UC1: Casual Gaming

Most users want to load a game and play. Minimal configuration.

```json
{
  "name": "Elite",
  "model": "model-b",
  "storage": {
    "fdc_socket": { "id": "acorn-1770" },
    "floppy_drives": [
      { "drive": 0, "image_uri": "file:///Users/bob/Discs/Elite.ssd" }
    ]
  },
  "startup_options": {
    "auto_boot": true
  }
}
```

Frontend hides complexity; user sees "Elite" in preset list, clicks Play.

### UC2: Development Environment

Developer needs specific ROMs, blank save disc, 80-column mode.

```json
{
  "name": "BASIC Development",
  "model": "model-b",
  "sideways_bank": {
    "slots": [
      { "slot": 15, "type": "rom", "image_uri": "library://roms/bbc-basic_2.rom" },
      { "slot": 14, "type": "rom", "image_uri": "library://roms/acorn-dfs_0_90.rom" },
      { "slot": 13, "type": "rom", "image_uri": "library://roms/view-wordprocessor.rom" }
    ]
  },
  "storage": {
    "fdc_socket": { "id": "acorn-1770" },
    "floppy_drives": [
      { "drive": 0, "image_uri": "file:///Users/bob/Discs/DevTools.ssd" },
      { "drive": 1, "image_uri": "file:///Users/bob/Discs/Work.ssd" }
    ]
  },
  "startup_options": {
    "screen_mode": 0
  }
}
```

### UC3: Classroom (Multi-Machine)

Teacher sets up multiple stations with Econet.

```json
{
  "name": "Student Station",
  "model": "master-128",
  "networking": {
    "econet": {
      "enabled": true,
      "station": "${STATION_NUMBER}"
    }
  },
  "startup_options": {
    "auto_boot": true
  }
}
```

(Note: Variable substitution is a potential future feature)

### UC4: Hardware Testing

Testing specific MOS version with minimal config.

```json
{
  "name": "MOS 1.00 Test",
  "model": "model-b",
  "os_rom": {
    "mos": "mos100"
  },
  "storage": {
    "fdc_socket": { "id": "none" }
  },
  "startup_options": {
    "screen_mode": 7
  }
}
```

### UC5: Second Processor Development

ARM development environment.

```json
{
  "name": "ARM Development",
  "model": "master-128",
  "coprocessor": {
    "type": "arm",
    "ram_kb": 4096
  },
  "storage": {
    "one_mhz_bus": {
      "devices": [
        {
          "id": "acorn-scsi",
          "hard_drives": [
            { "unit": 0, "scsi_id": 0, "image_uri": "file:///Users/bob/HardDiscs/ARM-Dev.hdf" }
          ]
        }
      ]
    }
  }
}
```

---

## Open Questions

### Schema Versioning

How to handle schema changes:
- Add new optional fields (backwards compatible)
- Rename fields (migration needed)
- Remove fields (deprecation period)

**Proposal**: `schema_version` field with migration support in frontends.

### Unknown Sections

When a frontend encounters an unknown section type:
1. Preserve it in the preset (don't lose data)
2. Display it as "Unknown: [type]" with raw JSON
3. Allow editing as raw JSON

This enables forward compatibility — old frontends can load presets from newer cores.

### Preset Sharing

How to handle presets that reference:
- Absolute file paths (not portable)
- ROM/image files that may be copyrighted

**Options**:
- Presets are local-only (no sharing)
- Presets with relative paths + ROM library concept
- Presets as "recipes" that don't include actual files
