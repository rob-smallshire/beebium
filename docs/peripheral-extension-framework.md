# Peripheral Extension Framework

## Overview

The BBC Micro supported diverse third-party peripherals connected via external ports (1 MHz bus, User Port, Analogue Port, RS-423, Tube). Beebium's peripheral extension framework provides a principled architecture for adding these peripherals as pluggable modules -- either compiled into the server executable (built-in) or loaded from shared libraries at startup (plugins).

Two properties of BBC Micro peripherals drive the framework design:

**Multi-port devices.** A single peripheral can connect to multiple interfaces simultaneously. The Voltmace Delta 14B/1 joystick interface connects to both the User Port and the Analogue Port. The framework models this with composition: one extension implementing multiple device callback interfaces.

**Sub-bus topologies.** Some peripherals introduce new buses. An Acorn SCSI Host Adapter plugs into the 1 MHz bus and provides a SCSI bus to which hard drives and LaserDisc players connect. An IEEE-488 adapter provides a GPIB bus. The framework supports this through dynamic extension points: an extension can declare that it provides a new named attachment site for other extensions.

For the SCSI use case that motivated this framework, see [Hard Disc Emulation Comparison](hard-disc-comparison.md).

## Architecture

### Extension Points

Extension points are named attachment sites where extensions can register. Built-in extension points are provided by the machine hardware; dynamic extension points are created by extensions during initialisation.

| Extension Point | Type | Provider |
|----------------|------|----------|
| `1mhz-bus` | Built-in | Machine hardware (FRED/JIM region, 0xFC00-0xFDFF) |
| `user-port` | Built-in | User VIA Port B, CB1/CB2 |
| `printer-port` | Built-in | User VIA Port A, CA1/CA2: the parallel printer port (see `docs/printer-port.md`) |
| `analogue-port` | Built-in | ADC (future) |
| `scsi` | Dynamic | Created by SCSI Host Adapter extension (future) |

Which built-in extension points are available depends on the machine variant. A Model A has none (no User VIA, no 1 MHz bus). A Model B has `1mhz-bus`, `user-port`, `analogue-port`, etc. The hardware policy class registers the extension points corresponding to its physical ports.

### Extensions

Each extension is a C++ class that inherits from `PeripheralExtension` and optionally implements device callback interfaces for the ports it uses. Extensions declare their dependencies (`attaches_to`) and what new extension points they create (`provides`).

```cpp
// src/core/include/beebium/extension/PeripheralExtension.hpp

class PeripheralExtension {
public:
    virtual ~PeripheralExtension() = default;

    // Manifest is the single source of truth for metadata.
    void set_manifest(ExtensionManifest manifest);
    const ExtensionManifest& manifest() const;
    virtual std::string_view name() const;         // default reads from manifest
    std::string_view description() const;          // reads from manifest

    // Dependencies and provisions
    virtual std::span<const std::string_view> attaches_to() const = 0;
    virtual std::span<const std::string_view> provides() const = 0;

    // Lifecycle
    virtual void init(ExtensionContext& ctx) = 0;
    virtual void shutdown() = 0;

    // Typed APIs (zero or more), served over the core's ExtensionRpc channel
    virtual std::vector<ExtensionRpcDispatcher*> rpc_dispatchers() { return {}; }
};
```

### Port Handles and Device Callbacks

I/O methods are not on `PeripheralExtension`. Each extension point type defines a **port handle** (owned by the machine) and a **device callback interface** (implemented by the extension). This composition-based design avoids diamond inheritance for multi-port devices.

Currently implemented:

```cpp
// src/core/include/beebium/extension/OneMHzBusPort.hpp

class OneMHzBusPort {
public:
    uint8_t read(uint16_t offset);                    // MemoryMappedDevice interface
    void write(uint16_t offset, uint8_t value);       // MemoryMappedDevice interface
    void claim_addresses(uint16_t base, uint16_t end, // register device for address range
                         OneMHzBusDevice& device);
    void tick();                                       // tick all registered devices
    bool is_claimed(uint16_t offset) const;
};

// src/core/include/beebium/extension/OneMHzBusDevice.hpp

struct OneMHzBusDevice {
    virtual ~OneMHzBusDevice() = default;
    virtual uint8_t read(uint16_t offset) = 0;        // offset relative to 0xFC00
    virtual void write(uint16_t offset, uint8_t value) = 0;
    virtual void tick() {}
};
```

The `offset` parameter in `OneMHzBusDevice` is relative to 0xFC00 (the start of the FRED page), matching what the MemoryMap's Region binding computes. FRED occupies offsets 0x0000-0x00FF; JIM occupies 0x0100-0x01FF. Unclaimed addresses return 0xFF on read (74LS245 transceiver behaviour).

Future port types (UserPort, AnaloguePort) will follow the same pattern: a port handle class and a device callback interface, added without changing existing code.

Where one chip serves two connectors, the hardware owns a composite `ViaPeripheral` and the port handles front its halves. The User VIA is the case in point: `UserViaPeripheral` is installed on the VIA once by the hardware policy, `UserPort::attach()` places a `UserPortDevice` on its Port B / CB1 / CB2 half, and Port A / CA1 / CA2 are reserved for the printer port.

### Dependency Resolution

The `ExtensionRegistry` collects extensions and resolves their dependency graph via topological sort (Kahn's algorithm) at startup:

1. Built-in extension points (registered by the hardware policy) are always satisfied
2. Extensions whose `attaches_to` dependencies are all satisfied are initialised first
3. After initialisation, any extension points they `provide` become available
4. This repeats until all extensions are initialised, or a cycle/unsatisfied dependency is detected

Extensions are shut down in reverse initialisation order.

### Typed APIs

An extension does not host a gRPC service of its own. It returns dispatchers from `rpc_dispatchers()`, and the core's single `ExtensionRpc` service routes each call to the right one by logical service and instance id; only serialized messages cross the plugin boundary. See [howto_write_a_peripheral_extension.md](howto_write_a_peripheral_extension.md), step 3.

A core `PeripheralExtensionService` (always present) lets frontends discover loaded extensions via a `ListExtensions` RPC.

### Extension Manifest

Each extension has a `manifest.json` file alongside its shared library. The manifest is the **single source of truth** for extension metadata, CLI syntax, and parameter schema:

```json
{
    "name": "scsi-hard-disc",
    "description": "A SCSI hard disc, kept in a pair of image files (.dat and .dsc).",
    "library": "scsi-hard-disc",
    "cli": "scsi-hdd",
    "attaches_to": ["scsi"],
    "parameters": [
        {"key": "scsi-id", "label": "SCSI ID", "type": "integer", "position": 0,
         "default": "0", "min": 0, "max": 7, "unique": true,
         "description": "SCSI target ID (0-7)"},
        {"key": "image", "label": "Disc image", "type": "filepath", "position": 1,
         "media": "hard-disc", "description": "Path to DAT disc image file"},
        {"key": "adapter-id", "label": "SCSI adapter", "type": "string",
         "provider_of": "scsi", "description": "ID of SCSI adapter to attach to"}
    ]
}
```

Fields:
- `name` -- canonical extension type name (used in presets, gRPC, dependency resolution)
- `display_name` -- what a user calls it (e.g. "SCSI Hard Disc")
- `description` -- one sentence a user understands, saying what it is or does;
  a configuration UI shows it beside the display name (`describe-preset-schema`
  carries both, and a test holds every shipped description to a sentence)
- `cli` -- short CLI flag name (e.g. `--scsi-hdd`); defaults to `name` if omitted
- `library` -- shared library filename stem (platform adds `.dylib`/`.so`/`.dll`)
- `attaches_to` -- the attachment point(s) this extension plugs into (e.g.
  `["serial-port"]`); a *list*, because one extension may attach to several
  points at once (e.g. an adapter spanning the analogue port and the user port).
  Must match the extension's own `attaches_to()` (a build-time test enforces it).
- `provides` -- new extension points this extension creates for others to attach
  to (e.g. the SCSI adapter's `["scsi"]`); omit if none.
- `parameters` -- schema for CLI/preset/gRPC configuration (see below)
- `groups` -- parameters that are alternatives to one another (see below); omit
  if none

Listing `attaches_to` in the manifest (not only in code) lets tools enumerate
"what can plug in here" without loading any plugin -- the basis of the
attachment-point discovery below.

Parameter schema fields:
- `key` -- parameter name
- `label` -- a short human-readable name, for a configuration UI's form
  (required of every shipped extension: a test checks each parameter has a
  `label` and a `description`)
- `description` -- one line of human-readable description (used in help, error
  messages and forms)
- `type` -- `string`, `integer`, `boolean`, `filepath` (a file path,
  resolved by the server before the extension is configured, by the rule in
  [preset-schema.md](preset-schema.md#file-references); the extension is given
  an absolute path, or a bare value it owns), or `dirpath` (a directory path,
  anchored in the same way but never searched for or extracted from a preset
  archive; a value naming an existing file is refused before the machine
  starts, and a configuration UI offers a directory chooser)
- `position` -- positional index (0, 1, ...); omit or -1 for keyword-only
- `required` -- whether the parameter must be provided (default: false)
- `default` -- default value if not provided
- `is_list` -- the parameter may be given several times; its values form a list
- `min`, `max` -- for an `integer`, the least and greatest values allowed
- `choices` -- the only values allowed, each a bare string or
  `{"value": ..., "label": ..., "description": ...}`; matched without regard to
  case and stored as declared. A choice's optional `description` is a sentence
  saying what choosing it means, for a UI to show beside the selected choice
  (e.g. the RTC's register layouts each state the years they hold; a test holds
  those years to the code's)
- `choices_from` -- the values a UI may offer come from a host enumeration
  (today `serial-ports`, as `list-serial-ports` lists them); the value stays
  free text
- `default_from` -- the value used when none is given is worked out on the
  host the server runs on (today `aun-map-file`: `BEEBIUM_AUN_MAP_FILEPATH`
  when set, else the per-user `aun-map.json`). `default` stays empty, so an
  exact-record preset writes nothing for it; `describe-extension --json` gives
  the resolved value as `effective_default`, which for any other parameter is
  its `default`. A name this server does not know leaves the `default`
- `excludes` -- keys that may not be given together with this one (declaring
  it on either side is enough; a default or an empty value does not count)
- `media` and `bundled` -- for a `filepath`, what it names; a parameter gives
  at most one of them (a manifest giving both is refused when loaded):
  - `media` (`hard-disc`, as the extension's storage names its media): a
    **medium**, searched for by bare name (in the launch's container -- beside
    or inside the preset -- then the per-user `discs/`) and copied into the
    machine's directory, so the guest's writes never reach the preset.
  - `bundled` (`rom`): a **ROM** this extension ships, searched for by bare
    name ending in its own `roms/` directory, which also limits the names that
    may be given, and read in place.
  - Neither: a **location** the extension owns and may create (AUN's
    `map-file`), anchored where it is written but never searched for; a bare
    value is the extension's own (`none`).
- `provider_of` -- the value is the `id` of the instance providing the named
  attachment point to attach to (empty: the last one)
- `unique` -- the value must differ among the instances on the same attachment
- `fields`, `item_format` -- for an `is_list` parameter whose items have parts,
  the parts and the item's string form (below)

A list parameter's items may have parts: an AUN peer is a net, a station, a host
and a port. `fields` names them, each with `key`, `label`, `description`, `type`
(`string`, `integer` or `boolean`), `required`, `default`, `min`, `max` and
`choices`, which mean what a parameter's do. `item_format` is the item's string
form, the one the command line takes: `{key}` placeholders between literal
separators. The AUN transport's `map` and `subnet` are the examples:

```json
{"key": "map", "type": "string", "is_list": true,
 "item_format": "{net}.{station}@{host}@{port}",
 "fields": [
     {"key": "net", "label": "Econet net", "type": "integer",
      "default": "0", "min": 0, "max": 255, "description": "..."},
     {"key": "station", "label": "Econet station", "type": "integer",
      "required": true, "min": 1, "max": 254, "description": "..."},
     {"key": "host", "label": "AUN host (IP address or name)", "required": true,
      "description": "..."},
     {"key": "port", "label": "AUN UDP port", "type": "integer",
      "required": true, "min": 1, "max": 65535, "description": "..."}]}
```

A preset may give an item as an object of its fields
(`{"station": 254, "host": "10.0.0.5", "port": 32768}`), as a configuration UI
writes it, or in its string form (`"254@10.0.0.5@32768"`), as the command line
takes it. Either way the framework checks each field and hands the extension
the string form, so an extension reads a list the same way whatever its source.
`resolve-preset` writes each item back in the form it was given.

The framework formats an object by replacing each placeholder with its field's
value, and parses a string by splitting it on the separators from left to
right, each at its first occurrence, so a field may not contain the separator
that ends it. The template must name every field once and put a separator
between two placeholders; a separator may repeat (`@` does in `map`). Only the
first field may be optional, and then a separator must follow it: the field may
be absent together with its separator, so `254@10.0.0.5@32768` is station 254
with no net. Such a field is present when its separator comes before the next
separator in the template. A manifest whose `fields` have no `item_format`, or
whose `item_format` cannot describe them, is refused.

A group gathers parameters that are alternatives to one another, so that at
most one of them may be given. The Acorn real-time clock's start time and offset
from host time are one:

```json
"groups": [
    {"key": "start", "label": "Clock start",
     "description": "Where the clock starts when the machine is switched on.",
     "parameters": ["time", "offset"],
     "none": {"label": "Host time",
              "description": "The clock follows this computer's clock."}}
]
```

Group fields:
- `key` -- the group's name, unique within the manifest
- `label`, `description` -- what a configuration UI shows for the group
- `parameters` -- the member keys, in the order a UI offers them
- `none` -- optional: the `label` and `description` of the state in which no
  member is given (the common case, where there is one, so a UI offers it
  first)

A configuration UI presents a group as one choice -- `none` first, if the group
names it, then each member -- and shows the chosen member's field. The group is
the one place the alternatives are declared: when the manifest is read, each
member's `excludes` gains the other members, so giving two of them is the
`excludes` error and a parameter's `excludes` (in `describe-extension --json`)
lists them. Keep `excludes` for a pair of parameters that are not alternatives
in a group. A parameter belongs to at most one group, and a member is neither
`required` nor given a `default`, since leaving every member out must stay
possible; a manifest that breaks these rules, or whose group names a key that is
not a parameter, is refused.

`min`/`max`, `choices`, `excludes` and `required`, and a list item's
`fields`, are enforced for the command line and presets alike;
`provider_of`, `unique` and the attachment points are checked before the
machine is built ([preset-schema.md](preset-schema.md#attachment)). Only add a
bound or a choice list the code really enforces, so that the form, the
validation and the extension agree. `describe-extension <name> --json` shows
every property.

Two framework-managed parameters are accepted by every extension and do not appear in the manifest:
- `id` -- instance identity (see below; overridable via `id=<value>`)
- `label` -- display name for GUIs (when not set, one composed from the manifest's `display_name`, else the `id`)

### Instance Identity

Every extension instance has an `id` (stable identity for referencing, logging, gRPC) and an optional `label` (display name for GUIs). An instance not given an `id` takes its extension's name, or `name-1`, `name-2`, ... when that is taken, never an `id` another instance was given explicitly; ids are assigned in one place, once the preset and the command line are merged, so the same configuration always names its instances the same way and `resolve-preset` writes them into the document.

Extension-specific identifiers use qualified names to avoid collision with `id`: e.g. `scsi-id` for the SCSI target number, `adapter-id` to reference a parent adapter.

### CLI Syntax

Each extension becomes a first-class `--<cli-name>` flag. Arguments are colon-separated with positional and keyword support:

```bash
beebium-model-b-plus start \
    --acorn-scsi \
    --scsi-hdd 0:/path/to/drive0.dat \
    --scsi-hdd 1:/path/to/drive1.dat
```

Plugin extensions are auto-loaded from `<exe-dir>/extensions` (the canonical install layout populated by `beebium_finalize_plugin`). `--extension-dir <path>` adds further search directories — see [Extension Search Paths](#extension-search-paths) below.

Positional arguments may also be written in keyword form (must be in the correct positional slot):

```bash
--scsi-hdd scsi-id=0:image=/path/to/drive.dat
```

Keyword-only arguments (for advanced options):

```bash
--acorn-scsi id=scsi-a
--scsi-hdd 0:/path/to/drive.dat:adapter-id=scsi-a:id=boot-disc:label=System Disc
```

Filepaths are always the last positional element so shell tab-completion works. A value that contains a colon — a URI like `file://`/`http://`, or a `host:port` — must be wrapped in double quotes, since the argument form splits on `:` (e.g. `image="file:///discs/drive.dat"`). An unquoted `scheme://…` is detected and rejected with a message pointing to quoting.

Multiple instances of the same extension are created via repeated flags. Each invocation creates a separate instance with its own config and auto-generated id.

### Preset File Integration

Presets can include an `extensions` array:

```json
{
    "name": "Model B+ with SCSI hard disc",
    "model": "model-b-plus",
    "extensions": [
        {"name": "acorn-scsi"},
        {"name": "scsi-hard-disc", "config": {"scsi-id": "0", "image": "scsi0.dat"}},
        {"name": "scsi-hard-disc", "config": {"scsi-id": "1", "image": "scsi1.dat"}}
    ]
}
```

Extensions from presets are applied as baseline configuration; CLI flags can add more.

### gRPC Discovery

The `PeripheralExtensionService.ListExtensions` RPC returns the loaded extensions with their full configuration, and the machine's attachment points:

```protobuf
message ListExtensionsResponse {
    repeated ExtensionInfo extensions = 1;              // providers first
    repeated AttachmentPointInfo attachment_points = 2; // catalogue order
}

message ExtensionInfo {
    string name = 1;                        // extension type
    string id = 2;                          // instance identity
    string label = 3;                       // this instance's name
    repeated string attaches_to = 4;
    repeated string provides = 5;
    map<string, string> config = 6;         // current configuration
    repeated ParameterSchemaInfo parameters = 7;  // the manifest's full schema
    string description = 8;
    bool has_ui = 9;
    repeated StorageDevice storage_devices = 10;
    string display_name = 11;               // the manifest's display name
    repeated Attachment attachments = 12;   // point + provider_id, per attaches_to
    repeated ParameterGroupInfo groups = 13;
}
```

The server is the single source of truth for what a frontend shows: an `AttachmentPointInfo` gives a point's `display_name`, `description` and occupancy from the [catalogue](#attachment-points) (the `tube` point is shown as "Coprocessor"), with the ids of the instances providing it; an `Attachment` names the instance an extension is attached to, as `ExtensionContext` chose it (a `provider_of` parameter, else the last provider loaded), so a frontend nesting a hard disc under its SCSI adapter never repeats that rule; and a `ParameterSchemaInfo` has every property `describe-extension --json` gives, `effective_default` included. A frontend can build its peripheral sidebar from this response alone.

### Built-in vs Plugin

The guiding principle follows the real hardware's internal/external boundary:

- **Internal hardware** (factory-fitted): built into the server executable
- **External hardware** (user-attached): loaded from a plugin

The same source code compiles to both a static library (for built-in use) and a shared library (for plugin use). No BBC Micro model (except the Master AIV, not yet emulated) had a built-in SCSI adapter — it was an external card. The SCSI adapter and hard disc target are plugins, loaded via `--acorn-scsi` / `--scsi-hdd`.

### Extension Search Paths

Beebium resolves available extensions from multiple sources in priority order. For each `--<cli-name>` the highest-priority source wins, so a user-supplied directory can replace a stock built-in or augment the default install.

| Priority | Source | When |
| --- | --- | --- |
| 1 (lowest) | Built-in extensions compiled into the server | Always |
| 2 | `<exe-dir>/extensions` (auto-detected) | If the directory exists |
| 3..N | Each `--extension-dir <path>` in CLI order | Always processed; later overrides earlier |

Notes:

- `--extension-dir` is repeatable. Each occurrence appends to the search list.
- The auto-detected `<exe-dir>/extensions` is included whether or not `--extension-dir` is given. It's the canonical install layout produced by `beebium_finalize_plugin` (see `cmake/BeebiumPlugin.cmake`).
- A user-supplied `--extension-dir` whose path does not exist is a hard error (`Extension directory does not exist: <path>`, exit code `EX_CONFIG = 78`). The auto-detected default keeps failing silently — it's reasonable for that path not to exist on a stripped-down install.
- An extension manifest with the same `cli` field as one in a lower-priority source replaces it. If the replacement has a non-empty `library` field, the plugin loader is used (so a user dir can swap in a forked dynamic build of a stock extension).

### Discovering Available Extensions

These commands surface the same resolved extension set without launching the emulator. All honour `--format pretty|tsv|jsonl`:

```bash
# Concise list of every available extension (cli flag + description)
beebium-model-b list-extensions

# Add user dirs to the search path; same priority rules as `start`
beebium-model-b list-extensions --extension-dir ~/my-beebium-plugins

# Full parameter schema for one extension; <name> matches CLI stem or canonical name
beebium-model-b describe-extension acorn-rtc
beebium-model-b describe-extension acorn-65c02-coprocessor

# `start --help` includes the same list in its "Extensions:" section
beebium-model-b start --help
```

`start --help` shows the section using the auto-default search paths only; it does not pre-scan user `--extension-dir` paths (kept side-effect-free). For a view that includes user paths, use `list-extensions --extension-dir ...`.

### Attachment Points

An **attachment point** is a place an extension can plug in: the serial port, the user port, the 1 MHz bus, the Tube, a SCSI bus. The catalogue gives each a display name and an *occupancy* range `[min..max]` -- how many extensions must and may attach. A connector is `0..1` (optional, at most one); a bus is `0..N` (unbounded). The range, rather than a single/multi flag, accommodates hardware in between -- e.g. a future machine with two Tube sockets is `0..2`. A configuration UI uses this to ask, for each point, *"which one, if any, of these extensions would you like to load?"*.

```bash
# The catalogue: id, display name, occupancy, description
beebium-model-b list-attachment-points

# Only the extensions that attach to a given point
beebium-model-b list-extensions --attaches-to serial-port
```

Because `attaches_to` lives in the manifest, both queries work **before any emulator is launched** -- the graphical clients run these CLI commands (parsing `--format jsonl`) the same way they run `create-preset`, since there is no server to talk to over gRPC at configuration time. (The gRPC `PeripheralExtensionService.ListExtensions` is a different view: the extensions *loaded* in a *running* server.)

The point ids are the same strings used by `attaches_to()` / `provides()` / `register_extension_point()`; their display names live in `AttachmentPointCatalogue.hpp`, which is the single place to add a new point (e.g. a future `analogue-port`).

## Developer Guide: Creating a Built-in Extension

This walkthrough uses TestScratchRam (8 bytes of RAM at 0xFC50-0xFC57) as the reference implementation.

### 1. Create the Extension Directory

```
src/extensions/my-extension/
    CMakeLists.txt
    MyExtension.hpp
    MyExtension.cpp
    my_extension.proto         (if providing gRPC services)
    MyExtensionService.hpp     (if providing gRPC services)
    manifest.json              (for plugin builds)
    plugin_entry.cpp           (for plugin builds)
```

Add the subdirectory to `src/extensions/CMakeLists.txt`:

```cmake
add_subdirectory(my-extension)
```

### 2. Define the Extension Class

The extension inherits from `PeripheralExtension` and the device callback interface for each port it uses:

```cpp
// MyExtension.hpp
#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/OneMHzBusDevice.hpp>
#include <beebium/extension/OneMHzBusPort.hpp>
#include <beebium/extension/PeripheralExtension.hpp>

namespace beebium {

class MyExtension : public PeripheralExtension,
                    public OneMHzBusDevice {
public:
    MyExtension();
    ~MyExtension() override;

    std::span<const std::string_view> attaches_to() const override {
        static constexpr std::string_view deps[] = {"1mhz-bus"};
        return deps;
    }

    std::span<const std::string_view> provides() const override { return {}; }

    void init(ExtensionContext& ctx) override;
    void shutdown() override;

    // OneMHzBusDevice
    uint8_t read(uint16_t offset) override;
    void write(uint16_t offset, uint8_t value) override;
};

}  // namespace beebium
```

**Key points:**
- `name()` is inherited from `PeripheralExtension` and reads from the manifest -- do not override it
- `attaches_to()` declares which extension points are needed
- `provides()` declares which new extension points this extension creates (empty for leaf extensions)
- The constructor and destructor must be out-of-line if the class has `unique_ptr` members with incomplete types (pimpl for gRPC service)

### 3. Implement the Extension

```cpp
// MyExtension.cpp
#include "MyExtension.hpp"

namespace beebium {

MyExtension::MyExtension() = default;
MyExtension::~MyExtension() = default;

void MyExtension::init(ExtensionContext& ctx) {
    ctx.get<OneMHzBusPort>().claim_addresses(0x60, 0x63, *this);
    // Create gRPC service here if needed
}

void MyExtension::shutdown() {
    // Clean up gRPC service here if needed
}

uint8_t MyExtension::read(uint16_t offset) {
    // Handle read at offset (relative to 0xFC00)
    return 0xFF;
}

void MyExtension::write(uint16_t offset, uint8_t value) {
    // Handle write at offset (relative to 0xFC00)
}

}  // namespace beebium
```

**Key points:**
- The extension does not set its own manifest or config: whoever creates it
  does -- the built-in table's factory through `instantiate_extension` (step 6),
  or a plugin's entry point (see Creating a Plugin Extension)
- In `init()`, claim addresses on the bus port via `ExtensionContext`
- Addresses are offsets relative to 0xFC00, not absolute 16-bit addresses
- `claim_addresses()` throws if the range overlaps with another device

### 4. Add a Typed API (Optional)

To let clients drive the device, define its messages in a `.proto` beside the
extension and return an `ExtensionRpcDispatcher` from `rpc_dispatchers()`; the
core's `ExtensionRpc` service carries the calls. Step 3 of
[howto_write_a_peripheral_extension.md](howto_write_a_peripheral_extension.md)
walks through it.

### 5. Build Configuration

```cmake
# src/extensions/my-extension/CMakeLists.txt

# Compile the proto's messages (if exposing a typed API); never its gRPC stub
beebium_compile_proto(
    TARGET beebium_ext_my_extension
    PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/my_extension.proto
    PROTO_PATH ${CMAKE_CURRENT_SOURCE_DIR}
)

# Static library (built-in)
add_library(beebium_ext_my_extension STATIC
    MyExtension.cpp
    ${beebium_ext_my_extension_PROTO_SRCS}
)

target_include_directories(beebium_ext_my_extension PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${beebium_ext_my_extension_OUT_DIR}
)

target_link_libraries(beebium_ext_my_extension PUBLIC
    beebium_core protobuf::libprotobuf
)
```

### 6. Register as Built-in

Most extensions should be plugins (next section). A built-in is for one the
server must recognise with no plugin directory at all; today that is `aun`
and `host-serial`, whose rationale `BuiltinExtensions.hpp` records. A
built-in has no `manifest.json`: it is one entry in the table in
`src/server/include/beebium/server/BuiltinExtensions.hpp`, a manifest built in
code and a factory that constructs the extension:

```cpp
#include "MyExtension.hpp"

// In builtin_extensions::detail::make_entries():
{
    ExtensionManifest m;
    m.name = "my-extension";
    m.display_name = "My Extension";
    m.description = "Description of my extension";
    m.cli_name = "my-extension";          // --my-extension k=v:...
    m.extension_kind = "peripheral";
    m.attaches_to = {"1mhz-bus"};
    m.parameters.push_back({"base", "integer", "first claimed offset",
                            -1, false, false, "96"});
    result.push_back({std::move(m),
                      [] { return std::unique_ptr<Extension>(new MyExtension()); }});
}
```

The entry needs nothing else. From there the server treats it as it treats a
plugin's manifest:

- `populate_extension_resolver` (`StartOptions.hpp`) adds every built-in
  manifest to the `ExtensionResolver` as its lowest-priority source, below
  `<exe-dir>/extensions` and any `--extension-dir`, so a plugin of the same
  name replaces the built-in. The resolver is what makes `--my-extension` a
  start option, what `list-extensions` and `describe-extension` report, and
  what a preset's `extensions` entry is checked against.
- `instantiate_extension` (`StartOptions.hpp`) creates each configured
  instance: from its plugin library when the manifest names one, otherwise
  from the built-in factory, then gives it its manifest and normalised config
  (`set_manifest`, `set_config`, `set_list_config`). So the factory only
  constructs; unlike a plugin's entry point, which is handed the manifest and
  sets it, it is given nothing.
- `assemble_machine` (`MachineAssembly.hpp`) registers the instance by its
  `extension_kind`: an `econet-transport` with the `EconetTransportRegistry`,
  a `peripheral` with the `ExtensionRegistry`, whose `resolve_and_init` then
  attaches it and calls `init()`.

Link the server executables against `beebium_ext_my_extension` in
`src/server/CMakeLists.txt`, beside `beebium_ext_aun` and
`beebium_ext_host_serial`.

## Developer Guide: Creating a Plugin Extension

A plugin extension uses the same source code as a built-in but adds a shared library target and a plugin entry point.

### 1. Add Plugin Entry Point

```cpp
// plugin_entry.cpp
#include "MyExtension.hpp"
#include <beebium/extension/ExtensionManifest.hpp>

extern "C" {

__attribute__((visibility("default")))
beebium::PeripheralExtension* beebium_create_extension(
        const beebium::ExtensionManifest& manifest) {
    auto* ext = new beebium::MyExtension();
    ext->set_manifest(manifest);
    return ext;
}

}
```

**Key points:**
- The entry point is `extern "C"` for `dlsym` lookup
- It receives the manifest (parsed from `manifest.json` by the `PluginLoader`)
- It returns a raw pointer -- the framework takes ownership via `std::unique_ptr`
- Use `__attribute__((visibility("default")))` to ensure the symbol is exported

### 2. Check a Configuration Before Launch (Optional)

What the manifest cannot say -- a value valid alone but not with another
parameter's, such as the Acorn real-time clock's start time against the years
its register layout holds -- the plugin can check before any machine is built,
by exporting a configuration check (`ValidateConfigFn`,
`beebium/extension/ConfigValidation.hpp`):

```cpp
BEEBIUM_PLUGIN_EXPORT
void beebium_validate_extension_config(
        const beebium::ExtensionManifest& manifest,
        const std::map<std::string, std::string>& config,
        const std::map<std::string, std::vector<std::string>>& list_config,
        std::vector<beebium::ConfigDiagnostic>& diagnostics) {
    // append {parameter key, message, Error | Warning}
}
```

It receives the instance's configuration already normalised against the
manifest (defaults filled in, types, bounds, choices and exclusions checked)
and appends what it finds, each a sentence about one parameter (or `""` for the
instance as a whole); it must not throw. The framework calls it for every
instance once the preset and command line are merged and the attachments are
sound (`validate_extension_configs`, in `fold_in_preset`), so `start` and
`resolve-preset` see the same diagnostics: a preset instance's are located at
its parameter (`/extensions/<i>/config/<key>`, or
`/econet/transport/parameters/<key>`), a command-line instance's prefixed with
its flag; `start` refuses on an error and prints a warning. Make anything the
launch would certainly refuse an error, judging what depends on the host (such
as its clock) as it is when the check runs -- the RTC refuses host time its
layout cannot hold -- and keep warnings for what the launch may still accept.
A plugin without the check validates nothing beyond its manifest. Read the
configuration in one function the check and the extension's `init` both call,
as the RTC's `AcornRtcConfig` does, so the two cannot disagree.

### 3. Add Manifest

```json
{
    "name": "my-extension",
    "description": "Description of my extension",
    "library": "my-extension"
}
```

The `library` field is the filename stem; the platform adds `.dylib`, `.so`, or `.dll`.

### 4. Add Shared Library Target

```cmake
# Append to src/extensions/my-extension/CMakeLists.txt

if(BEEBIUM_BUILD_SERVICE)
    add_library(beebium_plugin_my_extension SHARED
        MyExtension.cpp
        plugin_entry.cpp
        ${beebium_ext_my_extension_PROTO_SRCS}
        ${beebium_ext_my_extension_GRPC_SRCS}
    )

    target_include_directories(beebium_plugin_my_extension PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${beebium_ext_my_extension_OUT_DIR}
    )

    target_link_libraries(beebium_plugin_my_extension PRIVATE
        beebium_core gRPC::grpc++ protobuf::libprotobuf
    )

    set_target_properties(beebium_plugin_my_extension PROPERTIES
        PREFIX "" OUTPUT_NAME "my-extension"
    )

    add_custom_command(TARGET beebium_plugin_my_extension POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json
            $<TARGET_FILE_DIR:beebium_plugin_my_extension>/manifest.json
    )
endif()
```

### 5. Load at Runtime

```bash
beebium-model-b start --extension-dir /path/to/plugins --extension my-extension
```

The `PluginLoader` reads `manifest.json` from subdirectories of `--extension-dir`, then loads only the extensions named by `--extension`. Extensions not named are never loaded.

## Startup Sequence

```
1.  Server starts
2.  First-pass argument parsing: extract --extension-dir and --preset
3.  If --preset specified, load preset (including extensions section)
4.  Scan extension manifests from --extension-dir (no code loaded)
5.  Build CLI name lookup table from manifest cli fields
6.  Second-pass argument parsing: recognise --<cli-name> flags,
    parse colon-separated arguments against manifest parameter schemas
7.  Hardware policy class registers built-in extension points
    (e.g. "1mhz-bus" for Model B; nothing for Model A)
8.  Built-in extensions registered with auto-generated ids
9.  Plugin extensions loaded for each CLI/preset extension instance:
    - dlopen shared library, call beebium_create_extension(manifest)
    - set_config() with parsed config map (including auto-generated id)
10. Dependency graph built from attaches_to / provides declarations
11. Topological sort determines init order (error on cycles)
12. Extensions initialised in dependency order:
    - init() receives ExtensionContext with port handles and provider lookup
    - After init, providers registered in context for downstream extensions
13. grpc::Service* instances collected from all initialised extensions
14. PeripheralExtensionService created (core discovery service)
15. ServerBuilder registers all built-in + extension + discovery services
16. BuildAndStart() -- gRPC server begins accepting connections
17. Emulation begins
```

## Source Tree Layout

```
src/
  core/
    include/beebium/extension/
      PeripheralExtension.hpp     # Base class (identity, config, lifecycle)
      ExtensionManifest.hpp       # Manifest + parameter schema
      ExtensionArgParser.hpp      # Schema-driven CLI argument parser
      ExtensionContext.hpp         # Port handles + provider lookup
      ExtensionRegistry.hpp       # Dependency resolution and lifecycle
      OneMHzBusPort.hpp           # 1 MHz bus port handle
      OneMHzBusDevice.hpp         # Device callback interface for 1 MHz bus
      PluginLoader.hpp            # Manifest scanning and dlopen loading
    src/
      PluginLoader.cpp            # Platform-specific dlopen/LoadLibrary
      ExtensionArgParser.cpp      # Colon-separated argument parser
  extensions/
    CMakeLists.txt                # Umbrella for all extensions
    test-scratch-ram/             # TestScratchRam (test fixture)
      manifest.json
      ...
    acorn-scsi/                   # Acorn SCSI Host Adapter (plugin)
      manifest.json               # cli: "acorn-scsi"
      AcornScsiHostAdapter.hpp/cpp
      ScsiBus.hpp/cpp             # Bus protocol state machine
      ScsiTarget.hpp              # Target interface
      ScsiHardDisc.hpp/cpp        # Hard disc target
      HardDiskImage.hpp/cpp       # DAT+DSC file I/O
      ScsiHostAdapterService.hpp  # gRPC service
      scsi_host_adapter.proto
      plugin_entry.cpp
    scsi-hard-disc/               # SCSI Hard Disc Target (plugin)
      manifest.json               # cli: "scsi-hdd", parameters: scsi-id, image
      ScsiHardDiscExtension.hpp/cpp
      plugin_entry.cpp
  service/
    proto/
      peripheral_extension.proto  # Core discovery service (id, label, config, schema)
    include/beebium/service/
      PeripheralExtensionService.hpp
      Server.hpp                  # Accepts extension_services span
  server/
    include/beebium/server/
      ServerMain.hpp              # Subcommand dispatch
      StartOptions.hpp            # Start option parsing, extension search
      MachineAssembly.hpp         # Extension loading and registration
      PresetLoader.hpp            # Preset extensions section
  cmake/
    BeebiumProto.cmake             # Reusable proto compilation function
```

## References

- [Hard Disc Emulation Comparison](hard-disc-comparison.md) -- SCSI protocol details, controller comparisons, image formats, hardware references (BeebSCSI, Pi1MHz), AIV/VP415 LaserDisc support, iSCSI backend
- [Floppy Disc Image Format Comparison](floppy-disc-comparison.md) -- floppy controller comparisons across emulators
- [Disc Subsystem](disc-subsystem.md) -- WD1770 floppy architecture and the `DiscControllerSocket` pattern that inspired the extension registry
- [gRPC Server Interface](grpc-server.md) -- existing service definitions
- [Clock Architecture](clock-architecture.md) -- bus stretching for 1 MHz peripherals
