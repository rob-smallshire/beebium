# Phase 10: Welcome Window

> **Superseded** (2026-10, #136): a historical plan. Stored presets are now `.beebium-preset` zip archives; for the format and commands as they are, see [preset-schema.md](../preset-schema.md) and [cli.md](../cli.md#preset-management-subcommands).

**Goal**: Provide a welcoming startup experience with quick access to presets.

## 10.1 Window Design

```
┌───────────────────────────────────────────────┐
│                                               │
│              [Beebium Icon]                   │
│            Welcome to Beebium                 │
│                                               │
│  ┌───────────┐ ┌───────────┐ ┌───────────┐   │
│  │           │ │           │ │           │   │
│  │ [boot    ]│ │ [boot    ]│ │ [boot    ]│   │
│  │ [screen  ]│ │ [screen  ]│ │ [screen  ]│   │
│  │           │ │           │ │           │   │
│  ├───────────┤ ├───────────┤ ├───────────┤   │
│  │ Model B   │ │ Model B+  │ │ Model B   │   │
│  │   (DFS)   │ │           │ │  ROM/RAM  │   │
│  └───────────┘ └───────────┘ └───────────┘   │
│                                               │
│                              New Machine...   │
│                                               │
└───────────────────────────────────────────────┘
```

- Fixed size, non-resizable, centred on screen
- Preset cards show thumbnail image + model name
- "New Machine..." is a text button, right-aligned, visually subordinate
- No "Recent" section yet (deferred to saved state feature)
- User presets (future) appear alongside built-in presets

## 10.2 Interaction Model

**One-click launch**: Click a preset card to immediately launch the core and open the emulator
window. No select-then-create intermediate step. "New Machine..." opens the full New Machine
dialog for those who want to configure before launch.

## 10.3 Window Behaviour

- **Always shown on bare launch**: No preference to disable. The welcome window appears whenever
  Beebium is launched without a file argument.
- **Skipped when opening a file**: If a preset (or future saved state) is passed via CLI
  (`open -a Beebium --args --preset /path/to/file`) or file association (double-click
  `.preset.beebium` in Finder), skip the welcome window and go straight to the emulator window.
- **Auto-dismiss**: Closes itself when any emulator window appears (whether from a welcome window
  click, File > New..., or File > Connect...).
- **Reopen**: Window > Welcome to Beebium.
- **Close without action**: User gets an empty app with a menu bar. File > New... and
  File > Connect to Machine... remain available.

## 10.4 Thumbnails

### Capture

CLI `capture` subcommand on server executables. Reuses `parse_start_arguments` and machine
initialisation from `start`. Runs the emulator headlessly for a delay period, grabs a frame
from the internal framebuffer, writes PNG, exits. No gRPC server needed.

```bash
beebium-model-b capture --preset model-b.preset.beebium --output model-b.thumbnail.png
```

Optional `thumbnail_capture_delay_seconds` field in preset JSON controls the delay (default ~2s).
For built-in presets (boot screen), a short delay suffices. For user presets with disc images and
autoboot, a longer delay captures the game's title screen.

### Storage

Sidecar PNG with naming convention: `model-b.preset.beebium` → `model-b.thumbnail.png` in the
same directory. PresetManager discovers thumbnails by matching basename + `.thumbnail.png`.
Build step copies both preset and thumbnail files into `Beebium.app/Contents/Resources/presets/`.

Missing thumbnails fall back to a generic machine-type icon from the asset catalogue.

## 10.5 App Launch Architecture

```
App Launch
    │
    ├── Opened with file argument? ──yes──> Launch/open directly → emulator window (no welcome)
    │   (CLI --preset, double-click .preset.beebium, future: .state.beebium)
    │
    └── Bare launch (no arguments) ──> Show Welcome Window
                                           │
                                           ├── Click preset → launch core → open emulator window → auto-dismiss
                                           ├── Click "New..." → New Machine dialog → open emulator window → auto-dismiss
                                           └── Close welcome → empty app with menu bar
```

The main `WindowGroup` no longer auto-creates a window on launch. Emulator windows are created
only when triggered by the welcome window, File menu actions, or file open events.

## Files to create/modify
- `clients/macos/Beebium/Beebium/WelcomeWindow.swift` (new)
- `clients/macos/Beebium/Beebium/BeebiumApp.swift` (welcome window scene, conditional launch, Window menu)
- `clients/macos/Beebium/Beebium/ContentView.swift` (remove default connection fallback)
- `clients/macos/Beebium/Beebium/Presets/PresetManager.swift` (thumbnail discovery)
- `clients/macos/Beebium/project.yml` (bundle thumbnail PNGs)
- `src/server/include/beebium/server/ServerMain.hpp` (capture subcommand)
- `src/server/include/beebium/server/PresetLoader.hpp` (thumbnail_capture_delay_seconds field)

## Dependencies
- Phase 7.6 (Preset System) for preset data and PresetManager
- Phase 8 (New Machine Dialog) for machine creation flow

## Verification
- Launch app (bare) → Welcome Window appears (no connection attempt to default port)
- Click a preset → emulator window opens, emulation starts, welcome window auto-dismisses
- Click "New Machine..." → New Machine dialog opens
- Create machine via File > New... → welcome window auto-dismisses
- Close welcome window → empty app with menu bar, File menu works
- `open -a Beebium --args --preset /path/to/preset` → no welcome window, straight to emulator
- Window > Welcome to Beebium → reopens welcome window
- `beebium-model-b capture --preset model-b.preset.beebium --output test.png` → valid boot screen PNG
- Preset thumbnails display correctly in welcome window
- Missing thumbnail → generic machine icon fallback
