# Preset System Design

## Overview

The Preset System provides a way to define and manage machine configurations. A preset is a named configuration that can be used to launch a new emulator core with specific settings (model type, ROM selection, disc images, peripherals, etc.).

Presets serve two audiences:

1. **Casual users**: Pick "BBC Model B" or "BBC Master 128" and go
2. **Power users**: Create custom presets with specific configurations (e.g., "Model B with Watford DFS", "Master 128 with MOS 3.50")

This phase builds on:
- Phase 7.5 (Settings Infrastructure) — provides the UI home for preset management

## Design Principles

1. **Uniform discovery**: All presets are `.preset.beebium` files—bare machine defaults and configured variants alike
2. **Implementation detail hidden**: Users see "Built-in Machine Presets", not "executables vs preset files"
3. **User presets are copies**: Users duplicate system presets and modify; system presets are immutable
4. **Configuration comes from cores**: Cores self-describe their configuration schema via CLI
5. **Presets are portable**: Stored as JSON, can be shared between users
6. **Separation of concerns**: PresetManager is application-level; New Machine dialog just consumes it

## Preset Categories

### System Presets

System presets ship with the software and live in `$BEEBIUM_SERVERS_DIRPATH/presets/`:

```
presets/
├── model-b.preset.beebium              # bare (generated at build time)
├── model-b-plus.preset.beebium         # bare (generated at build time)
├── model-b-romram.preset.beebium       # bare (generated at build time)
├── model-b-with-acorn-dfs.preset.beebium
├── model-b-with-watford-dfs.preset.beebium
├── master-128.preset.beebium           # bare (generated at build time)
├── master-512.preset.beebium
├── master-turbo.preset.beebium
└── master-aiv.preset.beebium
```

**Bare presets** are minimal files generated at build time, one per executable. CMake invokes `create-preset --output` as a post-build step:

```bash
beebium-model-b create-preset --name "BBC Model B" --output presets/model-b.preset.beebium
```

This ensures preset files are created using the same code path as user presets, with the executable providing its own model ID, name, and release date:

```json
{
  "model": "model-b",
  "name": "BBC Model B",
  "release_date": "1981-12"
}
```

**Configured presets** add specific hardware configurations:
```json
{
  "name": "BBC Master 512",
  "description": "Master 128 with internal 80186 coprocessor for DOS compatibility",
  "model": "master-128",
  "release_date": "1986-10",
  "coprocessor": { "type": "master512" }
}
```

### User Presets

User presets are stored in platform-specific locations following OS conventions:

| Platform | Location |
|----------|----------|
| macOS    | `~/Library/Application Support/Beebium/presets/` |
| Linux    | `$XDG_CONFIG_HOME/beebium/presets/` (defaults to `~/.config/beebium/presets/`) |
| Windows  | `%APPDATA%\Beebium\presets\` |

**Note**: These directories are hidden by default on all platforms. Users interact with presets by name through the application UI, not by navigating to filesystem paths. The hidden directory is purely a storage backend.

**Environment variable override**: Set `BEEBIUM_USER_PRESETS_DIRPATH` to use a custom location for user presets. This is useful for advanced users, portable installations, or unusual setups.

User presets can be edited or deleted through the application UI.

## Preset Lifecycle

```
┌─────────────────────────────────────────────────────────────────┐
│                                                                 │
│    1. Find Server Directory                                     │
│       $BEEBIUM_SERVERS_DIRPATH or fallback                      │
│                     │                                           │
│                     ▼                                           │
│    2. Discover System Presets                                   │
│       Glob: presets/*.preset.beebium                            │
│       Parse: model, release_date, config sections               │
│                     │                                           │
│                     ▼                                           │
│    3. Resolve Executables                                       │
│       For each preset's model field:                            │
│         Find beebium-{model} executable                         │
│         Query describe-preset-schema for name/description       │
│         Skip preset if executable not found                     │
│         Disable preset if features not yet implemented          │
│                     │                                           │
│                     ▼                                           │
│    4. Load User Presets                                         │
│       From ~/Library/Application Support/Beebium/presets/       │
│       Same parsing, same executable resolution                  │
│                     │                                           │
│                     ▼                                           │
│    5. Merge & Sort                                              │
│       [System Presets] + [User Presets]                         │
│       Sort by release_date, then natural sort by name           │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

## Data Model

### MachinePreset

```swift
struct MachinePreset: Codable, Identifiable, Hashable {
    let id: UUID
    var name: String
    let coreExecutablePath: String  // Path to beebium-model-b, etc.
    var configuration: [String: ConfigValue]
    let source: Source  // Where this preset came from
    var iconName: String?  // SF Symbol or custom icon
    var releaseDate: String?  // "YYYY", "YYYY-MM", or "YYYY-MM-DD"

    // Derived from core's describe-preset-schema
    var modelName: String  // "BBC Model B", "BBC Master 128"
    var modelDescription: String?  // Brief description for UI

    // State flags
    var isEnabled: Bool  // False if preset uses unsupported features
    var disabledReason: String?  // Why preset is disabled

    enum Source: Codable, Hashable {
        case systemPreset   // From $BEEBIUM_SERVERS_DIRPATH/presets/
        case userPreset     // From ~/Library/Application Support/...
    }

    var isEditable: Bool { source == .userPreset }
    var isSystemProvided: Bool { source == .systemPreset }
}

enum ConfigValue: Codable, Hashable {
    case string(String)
    case int(Int)
    case bool(Bool)
    case path(String)  // File path, gets special UI treatment
    case choice(String, options: [String])  // Enum-like selection
}
```

### Configuration Schema

Cores describe their configurable options via the `describe-preset-schema` CLI subcommand. This enables the frontend to render appropriate UI without hardcoding knowledge of each model's options.

```bash
beebium-model-b describe-preset-schema
```

Output is JSON with a sectioned structure:

```json
{
  "schema_version": 1,
  "model": {
    "id": "model-b",
    "name": "BBC Model B",
    "description": "The original BBC Microcomputer with 32KB RAM"
  },
  "sections": [
    { "type": "storage", "builtin": {...}, "fdc_socket": {...}, "floppy_drives": [...] },
    { "type": "sideways_bank", ... },
    { "type": "coprocessor", ... }
  ]
}
```

See [preset-schema.md](../preset-schema.md) for the full schema specification.

### PresetManager

```swift
@MainActor
class PresetManager: ObservableObject {
    static let shared = PresetManager()

    @Published private(set) var defaultPresets: [MachinePreset] = []
    @Published private(set) var userPresets: [MachinePreset] = []

    var allPresets: [MachinePreset] {
        defaultPresets + userPresets
    }

    // Discovery
    func discoverCoreExecutables() async
    func refreshDefaultPresets() async

    // User preset management
    func duplicatePreset(_ preset: MachinePreset, newName: String) -> MachinePreset
    func updatePreset(_ preset: MachinePreset)
    func deletePreset(_ preset: MachinePreset)
    func resetToDefaults()  // Delete all user presets

    // Persistence
    private func loadUserPresets()
    private func saveUserPresets()

    // Core communication
    func fetchPresetSchema(for executablePath: String) async throws -> ConfigurationSchema
}
```

## Preset Discovery

### Search Locations

**System presets**:
1. `$BEEBIUM_SERVERS_DIRPATH/presets/` (environment variable)
2. `Beebium.app/Contents/Resources/presets/` (app bundle, production)
3. `~/Code/beebium/build/src/server/presets/` (development fallback)

**User presets** (platform-specific, can be overridden with `BEEBIUM_USER_PRESETS_DIRPATH`):
- macOS: `~/Library/Application Support/Beebium/presets/`
- Linux: `$XDG_CONFIG_HOME/beebium/presets/` (defaults to `~/.config/beebium/presets/`)
- Windows: `%APPDATA%\Beebium\presets\`

### Discovery Process

```swift
func discoverPresets() async {
    var systemPresets: [MachinePreset] = []
    var userPresets: [MachinePreset] = []

    // 1. Find system presets
    let systemPresetsDir = presetsDirectory()
    for presetFile in findPresetFiles(in: systemPresetsDir) {
        if let preset = await loadPreset(from: presetFile, source: .systemPreset) {
            systemPresets.append(preset)
        }
    }

    // 2. Find user presets
    let userPresetsDir = applicationSupportURL.appendingPathComponent("presets")
    for presetFile in findPresetFiles(in: userPresetsDir) {
        if let preset = await loadPreset(from: presetFile, source: .userPreset) {
            userPresets.append(preset)
        }
    }

    // 3. Sort by release date, then name
    let allPresets = systemPresets + userPresets
    self.presets = allPresets.sorted { comparePresets($0, $1) }
}

private func loadPreset(from url: URL, source: Source) async -> MachinePreset? {
    // Parse JSON
    guard let data = try? Data(contentsOf: url),
          let json = try? JSONDecoder().decode(PresetFile.self, from: data) else {
        NSLog("[PresetManager] Skipping invalid preset: \(url.lastPathComponent)")
        return nil
    }

    // Find matching executable
    let executablePath = serversDirpath() + "/beebium-\(json.model)"
    guard FileManager.default.isExecutableFile(atPath: executablePath) else {
        NSLog("[PresetManager] Skipping preset (no executable): \(json.model)")
        return nil
    }

    // Query executable for schema
    let (schema, _) = await fetchPresetSchema(from: executablePath)
    guard let schema = schema else { return nil }

    // Build preset
    return MachinePreset(
        id: UUID(),
        name: json.name ?? schema.model.name,
        coreExecutablePath: executablePath,
        source: source,
        releaseDate: json.releaseDate,
        modelName: schema.model.name,
        modelDescription: json.description ?? schema.model.description,
        isEnabled: checkFeatureSupport(json, schema),
        ...
    )
}

private func findPresetFiles(in directory: URL) -> [URL] {
    let fm = FileManager.default
    guard let contents = try? fm.contentsOfDirectory(at: directory, includingPropertiesForKeys: nil)
    else { return [] }
    return contents.filter { $0.pathExtension == "beebium" && $0.lastPathComponent.contains(".preset.") }
}
```

### File Naming Convention

Preset files use `.preset.beebium` extension:
- `model-b.preset.beebium` — bare Model B
- `model-b-with-acorn-dfs.preset.beebium` — Model B with Acorn DFS
- `master-512.preset.beebium` — Master 512 configuration

The `model` field inside the file determines which executable to use:
```json
{
  "model": "model-b",
  "release_date": "1981-12"
}
```

Executables follow the pattern `beebium-{model}`:
- `beebium-model-b`
- `beebium-model-b-plus`
- `beebium-master-128`

### Preset IDs

**The filename (minus extension) is the preset ID.** This provides a CLI-friendly identifier without spaces or special characters.

| Filename | Preset ID | Display Name |
|----------|-----------|--------------|
| `model-b.preset.beebium` | `model-b` | BBC Model B |
| `model-b-with-acorn-dfs.preset.beebium` | `model-b-with-acorn-dfs` | BBC Model B with Acorn DFS |
| `my-elite-setup.preset.beebium` | `my-elite-setup` | My Elite Setup |

The `name` field inside the preset file is purely for display purposes. The filename/ID is the stable identifier used for CLI operations and internal references.

**ID generation for user presets**: When a user creates a preset named "My Elite Setup", the application:
1. Slugifies the name → `my-elite-setup`
2. Saves as `my-elite-setup.preset.beebium`
3. Stores the original name in the `name` field for display

**Slug rules**: Preset IDs (filenames) must be valid slugs:
- Lowercase letters, numbers, and hyphens only
- No spaces or special characters
- Start with a letter or number
- The UI enforces these constraints during preset creation

## User Preset Storage

### File Location

User presets are stored in platform-specific directories (see [User Presets](#user-presets) above):

```
# macOS
~/Library/Application Support/Beebium/presets/
├── my-custom-model-b.preset.beebium
├── elite-setup.preset.beebium
└── testing-config.preset.beebium

# Linux
~/.config/beebium/presets/
├── my-custom-model-b.preset.beebium
├── elite-setup.preset.beebium
└── testing-config.preset.beebium

# Windows
%APPDATA%\Beebium\presets\
├── my-custom-model-b.preset.beebium
├── elite-setup.preset.beebium
└── testing-config.preset.beebium
```

### File Format

User preset files use the same JSON format as system presets:

```json
{
    "name": "My Custom Model B",
    "description": "Model B configured for Elite with Watford DFS",
    "model": "model-b",
    "release_date": "1981-12",
    "storage": {
        "fdc_socket": { "id": "watford-1770" }
    },
    "sideways_bank": {
        "slots": [
            { "slot": 14, "type": "rom", "image": "watford-dfs.rom" }
        ]
    }
}
```

The `name` field is the display name shown in the UI. The filename (ID) is derived from the name when the preset is created.

### File Naming

Preset files are named with a slugified ID and `.preset.beebium` extension:
- Display name: "My Custom Model B"
- Filename: `my-custom-model-b.preset.beebium`

Slugification rules:
- Convert to lowercase
- Replace spaces and special characters with hyphens
- Remove consecutive hyphens
- Ensure uniqueness (append `-2`, `-3`, etc. if needed)

## Settings UI (Machines Pane)

### Layout

```
┌─────────────────────────────────────────────────────────────────┐
│  Machine Presets                                                │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Built-in Machine Presets                                             │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │ 🖥️  BBC Model B                                      [Dup] ││
│  │     The original BBC Microcomputer with 32KB RAM            ││
│  │                                                             ││
│  │ 🖥️  BBC Model B with Acorn DFS                       [Dup] ││
│  │     Model B with Acorn 1770 FDC and DFS ROM                 ││
│  │                                                             ││
│  │ 🖥️  BBC Model B+                                     [Dup] ││
│  │     Enhanced Model B with 64KB RAM and built-in FDC         ││
│  │                                                             ││
│  │ 🖥️  BBC Master 128                                   [Dup] ││
│  │     The flagship BBC Micro with 128KB RAM                   ││
│  │                                                             ││
│  │ 🖥️  BBC Master 512                                   [Dup] ││
│  │     Master 128 with 80186 coprocessor for DOS               ││
│  │                                                             ││
│  │ 🖥️  BBC Master Turbo                          [Dup] (disabled)│
│  │     Master 128 with 65C102 second processor                 ││
│  │     ⚠️ Requires coprocessor support (not yet implemented)   ││
│  └─────────────────────────────────────────────────────────────┘│
│                                                                 │
│  My Machine Presets                                                     │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │ 🖥️  Elite Setup                            [Edit] [Delete] ││
│  │     BBC Model B • Custom configuration                      ││
│  │                                                             ││
│  │ 🖥️  Testing Config                         [Edit] [Delete] ││
│  │     BBC Master 128 • Custom configuration                   ││
│  └─────────────────────────────────────────────────────────────┘│
│                                                                 │
│  No user presets yet. Duplicate an available machine to start. │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

Note: System presets (bare machines + bundled configurations) are shown together in
"Built-in Machine Presets". The distinction between a bare executable and a configured preset
is an implementation detail hidden from users.

### Interactions

| Action | Trigger | Result |
|--------|---------|--------|
| Duplicate | Click [Dup] button | Creates user preset with name "[Original] Copy", opens edit sheet |
| Edit | Click [Edit] button | Opens edit sheet for configuration |
| Delete | Click [Delete] button | Confirmation alert, then removes preset |
| Refresh | Automatic on pane open | Re-scans for core executables |
| Import | File > Import Preset... | Copies a `.preset.beebium` file into user presets directory |
| Export | Right-click > Export... | Saves preset to user-chosen location for sharing |
| Reveal | Right-click > Show in Finder | Opens the presets directory in the system file browser |

### Preset Management

Since preset directories are hidden by default on all platforms, the application provides UI for all common operations:

**Creating presets**: Users create presets by duplicating a built-in preset and modifying it. The "Duplicate" action:
1. Prompts for a name (e.g., "My Elite Setup")
2. Generates a slug ID (e.g., `my-elite-setup`)
3. Creates `my-elite-setup.preset.beebium` in the user presets directory
4. Opens the preset editor

**Import**: The "Import Preset..." menu item or drag-and-drop allows users to add presets from external sources:
- Validates the preset file is valid JSON with required fields
- Copies the file to the user presets directory
- Handles name conflicts by appending numbers (e.g., `elite-setup-2.preset.beebium`)

**Export**: Right-clicking a preset and selecting "Export..." allows users to save a copy for sharing:
- Opens a save dialog
- Exports to the chosen location (outside the managed presets directory)

**Reveal in Finder/Explorer**: For power users who want direct filesystem access:
- Right-click any preset and select "Show in Finder" (macOS), "Show in Files" (Linux), or "Show in Explorer" (Windows)
- Opens the user presets directory in the system file browser

### Edit Sheet

```
┌─────────────────────────────────────────────────────────────────┐
│  Edit Preset                                                    │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Name: [Elite Setup                                          ]  │
│                                                                 │
│  Based on: BBC Model B                                          │
│                                                                 │
│  Configuration                                                  │
│  ─────────────────────────────────────────────────────────────  │
│                                                                 │
│  Operating System ROM     [▼ MOS 1.20                        ]  │
│                                                                 │
│  BASIC ROM                [▼ BASIC II                        ]  │
│                                                                 │
│  Disc Filing System       [▼ Acorn DFS 0.90                  ]  │
│                                                                 │
│  Drive 0 Disc Image       [None                         ] [📁] │
│                                                                 │
│  Drive 1 Disc Image       [None                         ] [📁] │
│                                                                 │
│  ☐ Enable Speech Chip                                           │
│                                                                 │
│                                    ┌────────┐ ┌──────────────┐  │
│                                    │ Cancel │ │     Save     │  │
│                                    └────────┘ └──────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

The configuration fields are generated dynamically from the core's `DescribeConfiguration` response:
- `CHOICE` → Dropdown picker
- `BOOLEAN` → Checkbox
- `FILE_PATH` → Text field + file picker button
- `STRING` → Text field
- `INTEGER` → Number field (with stepper if bounded)

## Integration with New Machine Dialog

The New Machine dialog (Phase 8) becomes a consumer of PresetManager:

```swift
struct NewMachineDialog: View {
    @EnvironmentObject var presetManager: PresetManager
    @State private var selectedPreset: MachinePreset?
    @State private var configuration: [String: ConfigValue] = [:]

    var body: some View {
        VStack {
            // Preset picker
            Picker("Preset", selection: $selectedPreset) {
                ForEach(presetManager.allPresets) { preset in
                    Text(preset.name).tag(Optional(preset))
                }
            }
            .onChange(of: selectedPreset) { preset in
                configuration = preset?.configuration ?? [:]
            }

            // Configuration editor (same as edit sheet)
            ConfigurationEditor(configuration: $configuration,
                               schema: selectedPreset?.schema)

            // Actions
            HStack {
                Button("Cancel") { dismiss() }
                Button("Create") { createMachine() }
                    .keyboardShortcut(.defaultAction)
            }
        }
    }
}
```

The key difference from Settings:
- Settings manages the *stored* presets
- New Machine dialog allows *temporary* modifications for a single launch

## Files to Create

| File | Purpose |
|------|---------|
| `clients/macos/Beebium/Beebium/PresetManager.swift` | Singleton managing preset discovery and storage |
| `clients/macos/Beebium/Beebium/MachinePreset.swift` | Preset data model |
| `clients/macos/Beebium/Beebium/Settings/MachinesSettingsPane.swift` | Full implementation (replaces placeholder) |
| `clients/macos/Beebium/Beebium/Settings/PresetEditSheet.swift` | Edit sheet for user presets |
| `clients/macos/Beebium/Beebium/ConfigurationEditor.swift` | Dynamic configuration form (shared with New Machine) |

## Files to Modify

| File | Changes |
|------|---------|
| `src/server/include/beebium/server/ServerMain.hpp` | Implement `describe-preset-schema` CLI subcommand |
| `clients/macos/Beebium/Beebium/Presets/ConfigurationSchema.swift` | Define schema types matching JSON output |
| `clients/macos/Beebium/Beebium/Presets/PresetManager.swift` | Invoke CLI and parse schema |
| `clients/macos/Beebium/Beebium/BeebiumApp.swift` | Add PresetManager to environment |

## CLI Interface

### Using Presets from the Command Line

Presets can be specified by ID (filename without extension) when launching an emulator:

```bash
# Launch with a system preset
beebium-model-b start --preset model-b-with-acorn-dfs

# Launch with a user preset
beebium-model-b start --preset my-elite-setup

# List available presets for this model
beebium-model-b list-presets
```

The `list-presets` subcommand outputs preset information in a human-readable format:

```
Built-in presets:
  model-b                    BBC Model B
  model-b-with-acorn-dfs     BBC Model B with Acorn DFS
  model-b-with-watford-dfs   BBC Model B with Watford DFS

User presets:
  my-elite-setup             My Elite Setup
  testing-config             Testing Config
```

For machine-readable output (e.g., for scripts), use `list-presets --json`.

### Preset Management Subcommands

GUIs and other clients invoke these subcommands rather than implementing preset management logic directly. This ensures consistent behavior across all clients.

```bash
# Query
beebium-model-b list-presets [--json]
beebium-model-b show-preset <id>
beebium-model-b report-presets-dirpath

# Mutate
beebium-model-b create-preset --name "My Elite Setup" [--from <source-id>]
beebium-model-b delete-preset <id>
beebium-model-b import-preset <filepath>
beebium-model-b export-preset <id> --output <filepath>
```

See [cli.md](../cli.md) for full documentation of each subcommand.

### describe-preset-schema Subcommand

Cores expose their configuration schema via CLI rather than gRPC. This allows schema introspection without starting a server.

```bash
beebium-model-b describe-preset-schema
```

Output is JSON (see [preset-schema.md](../preset-schema.md#describe-preset-schema) for the current specification):

```json
{
  "schema_version": 1,
  "model": {
    "id": "model-b",
    "name": "BBC Model B",
    "description": "The original BBC Microcomputer with 32KB RAM"
  },
  "sections": [
    {
      "type": "storage",
      "builtin": { "cassette": true, "fdc": null },
      "fdc_socket": { "options": [...] },
      "floppy_drives": [...]
    }
  ]
}
```

The macOS client invokes this subcommand for each discovered executable and parses the JSON to build its preset list.

## Testing

### Manual Verification

1. **Default preset discovery**
   - Launch app with core executables in bundle
   - Verify default presets appear in Settings > Presets
   - Verify preset names and descriptions match core responses

2. **User preset creation**
   - Duplicate a default preset
   - Verify copy appears in "Your Presets" section
   - Verify edit sheet opens with correct values

3. **User preset editing**
   - Modify configuration values
   - Save and re-open edit sheet
   - Verify changes persisted

4. **User preset deletion**
   - Delete a user preset
   - Verify it disappears from list
   - Verify file removed from disk

5. **Persistence across launches**
   - Create user presets
   - Quit and relaunch app
   - Verify user presets still present

6. **New Machine integration**
   - Open New Machine dialog
   - Verify all presets available in picker
   - Verify selecting preset populates configuration

### Unit Tests

```swift
final class PresetManagerTests: XCTestCase {

    func testDuplicatePreset() {
        let manager = PresetManager()
        let defaultPreset = MachinePreset(
            id: UUID(),
            name: "BBC Model B",
            coreExecutablePath: "/path/to/core",
            configuration: [:],
            isDefault: true,
            modelName: "BBC Model B",
            modelDescription: nil
        )
        manager.defaultPresets = [defaultPreset]

        let copy = manager.duplicatePreset(defaultPreset, newName: "My Copy")

        XCTAssertFalse(copy.isDefault)
        XCTAssertEqual(copy.name, "My Copy")
        XCTAssertEqual(copy.coreExecutablePath, defaultPreset.coreExecutablePath)
        XCTAssertNotEqual(copy.id, defaultPreset.id)
    }

    func testDeletePreset() {
        let manager = PresetManager()
        let userPreset = MachinePreset(
            id: UUID(),
            name: "Custom",
            coreExecutablePath: "/path/to/core",
            configuration: [:],
            isDefault: false,
            modelName: "BBC Model B",
            modelDescription: nil
        )
        manager.userPresets = [userPreset]

        manager.deletePreset(userPreset)

        XCTAssertTrue(manager.userPresets.isEmpty)
    }

    func testCannotDeleteDefaultPreset() {
        let manager = PresetManager()
        let defaultPreset = MachinePreset(
            id: UUID(),
            name: "BBC Model B",
            coreExecutablePath: "/path/to/core",
            configuration: [:],
            isDefault: true,
            modelName: "BBC Model B",
            modelDescription: nil
        )
        manager.defaultPresets = [defaultPreset]

        manager.deletePreset(defaultPreset)  // Should be no-op

        XCTAssertEqual(manager.defaultPresets.count, 1)
    }
}
```

## Edge Cases

### Missing Core Executables

If the app bundle doesn't contain any cores:
- Default presets list is empty
- Settings shows: "No machine cores found. Reinstall Beebium to restore default cores."
- New Machine dialog is disabled

### Core Version Mismatch

If a user preset references a configuration key that no longer exists in the core:
- Unknown keys are preserved but not shown in UI
- Warning shown: "Some configuration options are no longer available"
- Saving the preset removes the unknown keys

### Duplicate Preset Names

Duplicate names can occur when system and user presets share a name, or when importing presets:
- All presets with the same name are shown
- Duplicates are disambiguated with `(2)`, `(3)` suffixes in display
- When creating a new user preset, the UI disallows duplicate names
- When importing a preset with a duplicate name, append a number: "Elite" → "Elite (2)"

### Invalid File Paths in Presets

If a preset references a disc image that no longer exists:
- Configuration editor shows the path with warning icon
- Preset is still valid; the core will handle missing files

## Design Decisions

1. **Presets are JSON, not plist**: JSON is more portable and easier to share. Users can hand-edit if needed. File extension is `.preset.beebium`.

2. **Uniform preset discovery**: All machine configurations are preset files. Bare machine defaults (model-b.preset.beebium) are generated at build time. This simplifies client logic—one discovery mechanism for all presets.

3. **No visual distinction**: Users see "Built-in Machine Presets" with no indication whether a machine is a bare executable or a configured preset. This is an implementation detail.

4. **Sorting by era**: Presets are sorted chronologically by `release_date` (format: `YYYY`, `YYYY-MM`, or `YYYY-MM-DD`), then by natural alphanumeric name. Missing date components default to `00` for sorting.

5. **User presets own their configuration**: Rather than storing only deltas from defaults, user presets store complete configuration. This makes them independent of system preset changes.

6. **Configuration schema comes from cores**: The frontend doesn't hardcode knowledge of what options each model supports. This allows new core versions to add options without frontend changes.

7. **File paths are stored absolute**: Relative paths would be ambiguous. We accept that presets with paths may not be portable between machines, but that's acceptable for disc images (users can browse to new location).

8. **PresetManager is a singleton**: There's one canonical set of presets for the app. Both Settings and New Machine dialog reference the same manager.

9. **Duplicate names**: All presets with the same name are shown, disambiguated with `(2)`, `(3)` suffixes. The UI disallows creating new user presets with duplicate names.

10. **Unsupported features**: Presets using unimplemented features (e.g., coprocessor) are shown but disabled, with explanation.

11. **Broken presets**: Invalid JSON or missing `model` field → skip silently (log warning).

## Open Questions

1. **Preset icons**: Should presets have custom icons? Could use model icon for all, or allow bundled presets to specify custom icons.

2. **Preset validation**: How strict should validation be? Allow launching with missing ROM paths (core will error), or block in the UI?

3. **Recent configurations**: When user launches from New Machine with modifications (without saving as preset), should this be remembered as "last used configuration" per model?

4. **Core executable versioning**: If a core updates and its configuration schema changes, how do we migrate existing user presets? Probably best to leave unknown keys and let users update manually.

## Resolved Questions

1. **Preset export/import**: Yes, explicit Import/Export menu items plus drag-and-drop support. "Show in Finder/Explorer" for direct access.

2. **Build-time generation**: CMake generates bare preset files at build time. This ensures consistency with executables.

See the main [lifecycle-management.md](lifecycle-management.md) for the overall phase roadmap.
