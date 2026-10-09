# New Machine Dialog Design

## Overview

The New Machine Dialog provides File > New... functionality, allowing users to create and launch new emulator instances. It consumes the Preset System (Phase 7.6) to offer a streamlined machine creation experience.

This phase builds on:
- Phase 7.6 (Preset System) — provides PresetManager and machine configuration schema
- Phase 7.5 (Settings Infrastructure) — establishes UI patterns for configuration

## Implementation Phases

### Phase 8.1: Basic Launch (First Iteration)

The initial implementation focuses on the minimal path: select a preset, launch a machine.

**In scope:**
- Preset picker with all system and user presets
- Preset description display
- Create button launches core with preset defaults
- Cancel dismisses dialog
- Remember last-selected preset

**Deferred to Phase 8.2:**
- Drag-drop disc image onto dialog
- Configuration editing section
- Save as new preset option
- Configuration diff display

### Phase 8.2: Configuration Editing (Implemented)

Configuration editing UI with sidebar navigation:

**Implemented:**
- Disclosure-based expandable configuration section (▶ Configuration)
- Sidebar navigation for configuration sections
- Storage section with FDC picker and floppy drive slots
- Drive slots match StorageModeView visual style (drag-drop, browse, clear)
- Schema-driven FDC options (fetched from `describe-preset-schema`)
- "Save as new preset" checkbox with name field
- Dialog width animates when configuration expanded (380pt → 520pt)

**Files created:**
- `Configuration/ConfigurationEditor.swift` - Main editor with sidebar
- `Configuration/StorageSectionView.swift` - Storage section form
- `Configuration/FloppyDriveConfigView.swift` - Drive slot UI
- `Configuration/StorageConfigurationState.swift` - Mutable state model
- `Presets/StorageSchemaSection.swift` - Schema types for storage

## Design Principles

1. **Presets first**: The dialog leads with preset selection, not raw configuration
2. **Quick launch**: Common case (pick preset, hit Create) should be fast — no mandatory configuration
3. **Progressive disclosure**: Advanced configuration is available but not overwhelming (Phase 8.2)
4. **Temporary modifications**: Configuration changes in this dialog are ephemeral — they apply to the launched machine only, not to the stored preset (Phase 8.2)
5. **Shared components**: ConfigurationEditor is shared with Settings for consistency (Phase 8.2)

## User Workflow

### Phase 8.1: Primary Flow

1. User selects File > New... (⌘N)
2. Dialog appears with preset picker (last-selected preset remembered)
3. User selects a preset (or accepts the default)
4. User clicks Create
5. Core launches, connects, window opens

### Phase 8.1: Quick Launch Flow

1. User selects File > New... (⌘N)
2. Dialog appears with last-used preset selected
3. User presses Return (default action is Create)
4. Machine launches with preset defaults

### Phase 8.2: Configuration Flow (Future)

1. User selects File > New... (⌘N)
2. Dialog appears with preset picker
3. User expands configuration section
4. User modifies configuration for this launch
5. Optionally: user checks "Save as new preset"
6. User clicks Create
7. Core launches with modified configuration

## Dialog Layout

### Phase 8.1 Layout (First Iteration)

The initial dialog is minimal — preset selection and launch only:

```
┌─────────────────────────────────────────────────────────────────────┐
│  New Machine                                                        │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  Preset                                                             │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │ [icon] BBC Model B                                        ▾ │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                     │
│  The original BBC Microcomputer with 32KB RAM.                      │
│                                                                     │
│                                          ┌────────┐ ┌────────────┐  │
│                                          │ Cancel │ │   Create   │  │
│                                          └────────┘ └────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
```

### Phase 8.2 Layout (With Configuration)

Future iteration adds expandable configuration section:

```
┌─────────────────────────────────────────────────────────────────────┐
│  New Machine                                                        │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  Preset                                                             │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │ [icon] BBC Model B                                        ▾ │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                     │
│  The original BBC Microcomputer with 32KB RAM.                      │
│                                                                     │
│  ─────────────────────────────────────────────────────────────────  │
│                                                                     │
│  Configuration                                          [Disclose]  │
│                                                                     │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │ Storage                                                      │   │
│  │   Floppy Disc Controller    [Acorn 1770 FDC            ▾]   │   │
│  │   Drive 0                   [game.ssd              *] [📁]   │   │
│  │   Drive 1                   [None                 ] [📁]     │   │
│  │   * differs from preset                                      │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                     │
│  ☐ Save as new preset                                               │
│                                                                     │
│                                          ┌────────┐ ┌────────────┐  │
│                                          │ Cancel │ │   Create   │  │
│                                          └────────┘ └────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
```

### Preset Picker Detail

The preset picker uses a Menu-style picker (not a list) for compactness:

```
┌─────────────────────────────────────────────────────────────────┐
│ [icon] BBC Model B                                            ▾ │
├─────────────────────────────────────────────────────────────────┤
│ Built-in Machine Presets                                        │
│   [icon] BBC Model B                                       ✓    │
│   [icon] BBC Model B with Acorn DFS                             │
│   [icon] BBC Model B+                                           │
│   [icon] BBC Master 128                                         │
│ ─────────────────────────────────────────────────────────────── │
│ My Machine Presets                                              │
│   [icon] Elite Setup                                            │
│   [icon] Development Config                                     │
└─────────────────────────────────────────────────────────────────┘
```

The picker groups presets by source (system vs user) and shows checkmark on current selection.

## Data Flow

### Phase 8.1 Data Flow (Implemented)

```
┌─────────────────────────────────────────────────────────────────┐
│                                                                 │
│   PresetManager.shared                                          │
│   ┌─────────────────────────┐                                   │
│   │ systemPresets: [...]    │                                   │
│   │ userPresets: [...]      │                                   │
│   │ launchCore() → Result   │                                   │
│   └────────────┬────────────┘                                   │
│                │                                                │
│                ▼                                                │
│   NewMachineDialog                                              │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │ selectedPreset: MachinePreset?                          │   │
│   └────────────────────────┬────────────────────────────────┘   │
│                            │                                    │
│                            │ Create button clicked              │
│                            ▼                                    │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │ presetManager.launchCore(preset)                        │   │
│   │   → spawns: beebium-<model> start --preset <filepath>   │   │
│   │             --port 0 --advertise --wait=api             │   │
│   │   → parses stdout for "Listening on port XXXX"          │   │
│   │   → returns LaunchedCore(process:, port:)               │   │
│   └────────────────────────┬────────────────────────────────┘   │
│                            │                                    │
│                            ▼                                    │
│   ConnectWindowState.shared                                     │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │ pendingTarget = ConnectionTarget(127.0.0.1, port)       │   │
│   │ pendingNeedsRun = true                                  │   │
│   └────────────────────────┬────────────────────────────────┘   │
│                            │                                    │
│                            │ openWindow(id: "main")             │
│                            ▼                                    │
│   ContentView (new window)                                      │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │ .onAppear: consumePendingTarget() → (target, needsRun)  │   │
│   │ VideoClient.reconnect(to: target)                       │   │
│   │                                                         │   │
│   │ .onChange(connectionState == .connected):               │   │
│   │   → Connect all clients (keyboard, system, disc, ...)   │   │
│   │   → debuggerClient.connect(channel:)                    │   │
│   │   → if needsRun: debuggerClient.run()  // Start emu     │   │
│   └─────────────────────────────────────────────────────────┘   │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### Phase 8.2 Data Flow (Future)

Adds configuration editing and save-as-preset:

```
│   NewMachineDialog                                              │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │ selectedPreset: MachinePreset?                          │   │
│   │ configuration: [String: ConfigValue]  ← working copy    │   │
│   │ saveAsNewPreset: Bool                                   │   │
│   └────────────────────────┬────────────────────────────────┘   │
│                            │                                    │
│                            │ Create button clicked              │
│                            ▼                                    │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │ 1. Build launch arguments from configuration            │   │
│   │ 2. If saveAsNewPreset: save to user presets dir         │   │
│   │ 3. Launch core executable with arguments                │   │
│   │ 4. Connect to launched core via gRPC                    │   │
│   │ 5. Open machine window                                  │   │
│   └─────────────────────────────────────────────────────────┘   │
```

## State Management

### Phase 8.1 State (Implemented)

```swift
struct NewMachineDialog: View {
    @Environment(\.dismiss) private var dismiss
    @Environment(\.openWindow) private var openWindow

    @StateObject private var presetManager = PresetManager.shared
    @ObservedObject private var windowState = ConnectWindowState.shared

    // Preset selection - persisted via @AppStorage
    @AppStorage("lastSelectedPresetId") private var lastSelectedPresetId: String = ""
    @State private var selectedPreset: MachinePreset?

    // Launch state
    @State private var isLaunching = false
    @State private var launchError: String?
}
```

### ContentView State for Run() Coordination

```swift
struct ContentView: View {
    @StateObject private var debuggerClient = DebuggerClient()
    /// Whether this window needs to call Run() after connection (for cores launched with --wait=api)
    @State private var needsRun: Bool = false

    // ... in .onAppear:
    let (target, runNeeded) = ConnectWindowState.shared.consumePendingTarget()
    needsRun = runNeeded

    // ... in .onChange(of: videoClient.connectionState) when connected:
    if needsRun {
        needsRun = false
        Task {
            try? await debuggerClient.run()
        }
    }
}
```

### Phase 8.2 State (Future)

```swift
// Additional state for configuration editing
@State private var configuration: [String: ConfigValue] = [:]
@State private var configurationExpanded = false
@State private var saveAsNewPreset = false
@State private var newPresetName = ""
```

## Configuration Schema Integration (Phase 8.2)

This section describes the future configuration editing functionality.

The dialog will use the preset's associated configuration schema to render appropriate controls. The schema comes from the core's `describe-preset-schema` CLI command, already fetched by PresetManager during discovery.

### Schema to UI Mapping

| Schema Type | UI Control |
|-------------|------------|
| `storage.fdc_socket` | Dropdown picker (from `fdc_socket.options`) |
| `storage.floppy_drives[n].image_uri` | Text field + file picker button |
| `sideways_bank.slots[n]` | ROM slot configuration (future) |
| Boolean properties | Checkbox |
| Enum properties | Dropdown picker |
| String properties | Text field |
| Integer properties | Number field with stepper |

### ConfigurationEditor Component

```swift
struct ConfigurationEditor: View {
    @Binding var configuration: [String: ConfigValue]
    let schema: PresetSchema

    var body: some View {
        Form {
            // Group by schema section
            ForEach(schema.sections, id: \.type) { section in
                Section(section.displayName) {
                    SectionEditor(section: section, configuration: $configuration)
                }
            }
        }
    }
}
```

This component is shared between:
- NewMachineDialog (temporary configuration for launch)
- PresetEditSheet in Settings (persistent configuration changes)

## Launch Process

### Phase 8.1: Complete Launch Sequence

The launch uses `--wait=api` mode for controlled startup. This ensures the emulator doesn't start running until the window is fully connected:

```
1. User clicks Create in NewMachineDialog
        │
        ▼
2. PresetManager.launchCore() spawns core process
   Arguments: start --preset <filepath> --port 0 --advertise --wait=api
        │
        ▼
3. Parse stdout for "Listening on port XXXX"
   (5 second timeout, terminate process on failure)
        │
        ▼
4. Set ConnectWindowState:
   - pendingTarget = ConnectionTarget(host: "127.0.0.1", port: <discovered>)
   - pendingNeedsRun = true
        │
        ▼
5. Open new window: openWindow(id: "main")
   Dismiss dialog
        │
        ▼
6. ContentView.onAppear:
   - Consumes pendingTarget and needsRun flag
   - VideoClient.reconnect(to: target)
        │
        ▼
7. VideoClient connects → onChange triggers client cascade:
   - keyboardClient.connect(channel:)
   - systemClient.connect(channel:)
   - indicatorClient.connect(channel:)
   - discClient.connect(channel:)
   - audioClient.connect(channel:)
   - debuggerClient.connect(channel:)
        │
        ▼
8. If needsRun flag was set:
   - Call debuggerClient.run() via DebuggerControl.Run() RPC
   - This unblocks the emulator from --wait=api state
        │
        ▼
9. Emulator begins executing, display updates
```

### Key Implementation Details

**PresetManager.launchCore()** handles process spawning and port discovery:

```swift
func launchCore(_ preset: MachinePreset) async -> Result<LaunchedCore, CoreLaunchError> {
    let process = Process()
    process.executableURL = URL(fileURLWithPath: preset.coreExecutablePath)

    let arguments = [
        "start",
        "--preset", preset.presetFilepath,
        "--port", "0",        // Auto-assign port
        "--advertise",        // Enable Bonjour discovery
        "--wait=api"          // Wait for Run() RPC before starting
    ]

    // ... launch process, parse stdout for port, return LaunchedCore
}
```

**ConnectWindowState** passes the connection target and run-needed flag between views:

```swift
@MainActor
class ConnectWindowState: ObservableObject {
    static let shared = ConnectWindowState()

    @Published var pendingTarget: ConnectionTarget?
    @Published var pendingNeedsRun: Bool = false

    func consumePendingTarget() -> (ConnectionTarget?, Bool) {
        let target = pendingTarget
        let needsRun = pendingNeedsRun
        pendingTarget = nil
        pendingNeedsRun = false
        return (target, needsRun)
    }
}
```

**DebuggerClient** calls the Run() RPC to start emulation:

```swift
@MainActor
final class DebuggerClient: ObservableObject {
    private var client: Beebium_DebuggerControlNIOClient?

    func connect(channel: GRPCChannel) {
        client = Beebium_DebuggerControlNIOClient(channel: channel)
    }

    func run() async throws {
        guard let client = client else { throw DebuggerClientError.notConnected }
        _ = try await client.run(Beebium_Empty()).response.get()
    }
}
```

### Why --wait=api?

The `--wait=api` flag provides several benefits:

1. **Controlled startup**: The emulator waits in a paused state until the client is ready
2. **No missed frames**: The display window is guaranteed to be connected before emulation begins
3. **Clean initial state**: The user sees the machine boot from the beginning

Without `--wait=api`, there's a race condition where the emulator might boot partway before the window connects, causing the user to miss the initial boot sequence.

### Phase 8.2: Extended Launch (Future)

With configuration editing, the launch sequence adds:

1. Optionally save as new preset before launch
2. Build launch arguments from modified configuration (CLI overrides)
3. Pass additional `--config` arguments for non-preset values

The core launch mechanism (`--wait=api` + `Run()` RPC) remains the same.

## Interactions

### Phase 8.1 Interactions

| Action | Trigger | Result |
|--------|---------|--------|
| Select preset | Click picker, choose preset | Description updates, selection remembered |
| Cancel | Click Cancel or press Escape | Dialog dismisses, no action |
| Create | Click Create or press Return | Launch sequence begins |

### Phase 8.2 Interactions (Future)

| Action | Trigger | Result |
|--------|---------|--------|
| Expand config | Click disclosure button | Configuration section expands with editable fields |
| Browse for disc | Click folder button next to drive | File picker opens, filtered to disc image types |
| Toggle save | Check "Save as new preset" | Name field appears below checkbox |

## Keyboard Navigation

| Key | Action |
|-----|--------|
| ⌘N | Opens dialog (from menu) |
| Return | Create (default action) |
| Escape | Cancel |
| Tab | Navigate between controls |
| Space | Toggle checkboxes, activate buttons |

## Disabled Presets

Presets with unsupported features (e.g., coprocessor not yet implemented) appear in the picker but are disabled:

```
┌─────────────────────────────────────────────────────────────────┐
│ Built-in Machine Presets                                        │
│   [icon] BBC Model B                                       ✓    │
│   [icon] BBC Master Turbo                            (disabled) │
│          ⚠️ Requires coprocessor support                        │
└─────────────────────────────────────────────────────────────────┘
```

Selecting a disabled preset:
- Shows the description with warning
- Disables the Create button
- Explains why in the description area

## Files to Create/Modify

### Phase 8.1 (Implemented)

| File | Purpose |
|------|---------|
| `clients/macos/Beebium/Beebium/NewMachineDialog.swift` | Complete dialog with preset picker and launch flow |
| `clients/macos/Beebium/Beebium/Presets/PresetManager.swift` | Added `launchCore()` method and `CoreLaunchError` type |
| `clients/macos/Beebium/Beebium/DebuggerClient.swift` | New client for DebuggerControl service (Run() RPC) |
| `clients/macos/Beebium/Beebium/Generated/debugger.pb.swift` | Generated protobuf bindings for debugger.proto |
| `clients/macos/Beebium/Beebium/Generated/debugger.grpc.swift` | Generated gRPC client for DebuggerControl service |
| `clients/macos/Beebium/Beebium/ConnectDialog.swift` | Extended ConnectWindowState with `pendingNeedsRun` flag |
| `clients/macos/Beebium/Beebium/ContentView.swift` | Added DebuggerClient, consumes needsRun flag, calls Run() |

### Phase 8.2 (Configuration Editing)

| File | Purpose |
|------|---------|
| `clients/macos/Beebium/Beebium/ConfigurationEditor.swift` | Shared configuration form component |
| `clients/macos/Beebium/Beebium/ConfigurationSectionEditor.swift` | Per-section form rendering |
| `clients/macos/Beebium/Beebium/Presets/PresetManager.swift` | Add `createPreset(basedOn:name:configuration:)` method |
| `clients/macos/Beebium/Beebium/Settings/PresetEditSheet.swift` | Refactor to use shared ConfigurationEditor |

## Integration Points

### With PresetManager

The dialog uses PresetManager for:
- `presetManager.systemPresets` — built-in machine presets
- `presetManager.userPresets` — user-created presets
- `presetManager.launchCore()` — spawns core process with `--wait=api`
- Preset's associated `PresetSchema` for configuration rendering (Phase 8.2)

### With ConnectWindowState

The dialog communicates with ContentView via ConnectWindowState:
- Sets `pendingTarget` with the discovered port
- Sets `pendingNeedsRun = true` to trigger Run() RPC after connection

### With DebuggerClient

ContentView uses DebuggerClient to:
- Call `Run()` RPC to start emulation after all clients connect
- This unblocks the core from `--wait=api` state

### With FileCommands

FileCommands (File menu) presents the dialog:

```swift
struct FileCommands: Commands {
    @Environment(\.openWindow) var openWindow

    var body: some Commands {
        CommandGroup(after: .newItem) {
            Button("New...") {
                openWindow(id: "new-machine")
            }
            .keyboardShortcut("n", modifiers: .command)
        }
    }
}
```

## Testing

### Phase 8.1 Manual Verification

1. **Dialog presentation**
   - File > New... opens the dialog
   - ⌘N keyboard shortcut works
   - Dialog appears as a sheet or modal

2. **Preset selection**
   - All system presets appear in picker
   - All user presets appear in picker (in separate section)
   - Selecting preset updates description
   - Last-selected preset is remembered across dialog opens

3. **Quick launch**
   - With first preset selected, pressing Return creates machine
   - Machine window opens after creation
   - Core process is running

4. **Disabled presets**
   - Disabled presets appear grayed out
   - Selecting disabled preset disables Create button
   - Warning explains why preset is unavailable

5. **Error handling**
   - Missing executable shows error
   - Launch failure shows error message
   - Error can be dismissed, user can try again

### Phase 8.2 Manual Verification (Future)

1. **Configuration editing**
   - Expand/collapse works
   - FDC picker shows available options
   - File picker filters to appropriate image types
   - Changes don't affect stored preset
   - Modified values show diff indicator

2. **Save as preset**
   - Checking box shows name field
   - Creating with save creates new user preset
   - New preset appears in Settings > Machines

### Unit Tests

```swift
final class NewMachineDialogTests: XCTestCase {

    // Phase 8.1 tests
    func testBuildLaunchArguments_presetOnly() {
        let preset = makeTestPreset(id: "model-b")

        let args = buildLaunchArguments(preset: preset)

        XCTAssertEqual(args, ["start", "--preset", "model-b", "--port", "0"])
    }

    func testDisabledPresetBlocksCreate() {
        // Selecting a disabled preset should disable the Create button
    }

    // Phase 8.2 tests (future)
    func testBuildLaunchArguments_withFdcOverride() {
        // ...
    }

    func testPresetSelectionResetsConfiguration() {
        // When user selects a different preset, configuration resets
        // to that preset's defaults (any modifications are discarded)
    }
}
```

## Edge Cases

### No Presets Available

If PresetManager has no presets (no cores found):
- Dialog shows error state
- "No machine cores found. Reinstall Beebium to restore default cores."
- Create button disabled
- Cancel is the only available action

### Core Launch Failure

If the core executable fails to start:
- Show error in dialog (don't dismiss)
- Allow user to try again or cancel
- Log details for debugging

### Connection Timeout

If gRPC connection fails after launch:
- Attempt to terminate the launched process
- Show error message
- Offer retry or cancel

### Preset Deleted During Dialog

If the selected preset is deleted (via Settings) while dialog is open:
- Dialog should handle gracefully
- Either select another preset or show error

### Invalid Configuration (Phase 8.2)

If configuration values are invalid (e.g., invalid file path):
- Validation happens at launch time (core reports error)
- Show core's error message
- Allow user to fix and retry

## Design Decisions

1. **Phased implementation**: Start with basic preset selection and launch (Phase 8.1), add configuration editing later (Phase 8.2). Get the core workflow working first.

2. **Modal dialog**: The New Machine dialog is modal because it's initiating an action (launching a core). Non-modal would allow multiple dialogs, which is confusing.

3. **Picker over list**: Using a dropdown picker rather than a list view keeps the dialog compact. The full preset list with descriptions is available in Settings.

4. **Configuration collapsed by default** (Phase 8.2): Most users will just pick a preset and go. Advanced configuration is available but hidden initially.

5. **Temporary modifications** (Phase 8.2): Changes in this dialog don't persist. This matches user expectation — "I'm launching a machine, not editing a preset." The "Save as new preset" option provides an escape hatch.

6. **Schema-driven UI** (Phase 8.2): Configuration controls are generated from the schema. This means new configuration options from updated cores appear automatically.

7. **Preset-based launch arguments**: Even with modifications, we launch with `--preset` plus overrides. This keeps the argument list manageable and lets the core apply preset defaults for anything not overridden.

8. **Shared ConfigurationEditor** (Phase 8.2): The same component handles configuration in both New Machine dialog and Settings. This ensures consistency and reduces maintenance.

## Resolved Questions

1. **Remember last preset**: Yes — use UserDefaults to remember the last-selected preset. (Phase 8.1)

2. **Preset preview image**: No — not adding thumbnail images to presets at this time.

3. **Configuration diff display**: Yes — when configuration editing is added (Phase 8.2), show which values differ from the preset defaults.

## Dependencies

### Phase 8.1 (Implemented)

- Phase 7.6 (Preset System) — PresetManager, MachinePreset
- ConnectWindowState — passes connection target between views
- DebuggerClient — calls Run() RPC to start emulation
- debugger.proto — DebuggerControl service definition with Run() RPC

### Phase 8.2

- PresetSchema — for rendering configuration controls
- Phase 7.5 (Settings Infrastructure) — UI patterns, AppSettings
- ConfigurationEditor — shared component with Settings

## See Also

- [lifecycle-management.md](lifecycle-management.md) — overall phase roadmap
- [preset-system.md](preset-system.md) — preset discovery and management
- [preset-schema.md](../preset-schema.md) — the preset file format, resolve-preset and describe-preset-schema
