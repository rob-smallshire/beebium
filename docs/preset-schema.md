# Preset Schema

A **preset** is a JSON file (`<id>.preset.beebium`) that describes one machine
configuration: the model, the ROMs, the disc and network hardware, the
extensions plugged into it, and how it starts. `start --preset <id|filepath>`
launches the machine a preset describes. A front-end's New Machine dialog
edits a preset as a draft, launches it by writing it to a file and passing
`--preset`, and saves it with `create-preset`.

This document is the reference for the file format as the server implements
it (`src/server/include/beebium/server/PresetLoader.hpp`, `apply_preset` and
the merge steps in `ServerMain.hpp`). Where the system and user presets live,
and how an id is found, is in [deployment.md](deployment.md#preset-discovery). The
commands that list, show, create, import, export and delete presets are in
[cli.md](cli.md#preset-management-subcommands).

## Contents

- [An example](#an-example)
- [Top-level keys](#top-level-keys)
- [Paths](#paths)
- [Machine settings](#machine-settings): `speed`, `startup`, `mos_image_uri`
- [Storage](#storage)
- [Sideways ROM/RAM bank](#sideways-romram-bank)
- [Econet](#econet)
- [Extensions](#extensions), [attachment](#attachment)
- [Which models accept what](#which-models-accept-what)
- [Command line over preset](#command-line-over-preset)
- [Diagnostics](#diagnostics)
- [resolve-preset](#resolve-preset)
- [describe-preset-schema](#describe-preset-schema)
- [Not in a preset](#not-in-a-preset)

## An example

The shipped Level 3 File Server preset, as `create-preset` writes it:

```json
{
  "model": "model-b",
  "name": "Station 254 (L3 File Server, Model B)",
  "release_date": "1985",
  "description": "Level 3 File Server: ADFS + ANFS, SCSI disc, 65C02 copro",
  "machine_name": "L3 File Server, station {econet-station}",
  "startup": { "auto_boot": true },
  "storage": { "fdc_socket": { "id": "acorn-1770" } },
  "sideways_bank": {
    "slots": [
      { "slot": 9, "type": "rom", "image_uri": "acorn-anfs_4_18.rom" },
      { "slot": 10, "type": "rom", "image_uri": "acorn-adfs_1_30.rom" }
    ]
  },
  "econet": {
    "station": 254,
    "transport": { "name": "aun", "parameters": { "discovery": "on", "net": "0", "port": "0" } }
  },
  "extensions": [
    { "name": "acorn-rtc", "config": { "layout": "7bit-year-in-r7" } },
    { "name": "acorn-scsi" },
    { "name": "scsi-hard-disc", "config": { "image": "l3fs-v1_26b.dat", "scsi-id": 0 } },
    { "name": "acorn-65c02-coprocessor" }
  ]
}
```

Every key is optional except that a preset launched by a model's executable
must name that model if it names one. A key a preset leaves out takes the
model's default, which `resolve-preset` shows.

## Top-level keys

| Key | Type | Meaning |
|-----|------|---------|
| `model` | string | The machine type, e.g. `model-b`, `model-b-plus`. Must match the executable's model, or the launch fails with `CONFIG` (78) |
| `name` | string | The preset's display name. Default: the file name without its suffix |
| `release_date` | string | `YYYY`, `YYYY-MM` or `YYYY-MM-DD`, for sorting in a picker |
| `description` | string | One line for a preset picker |
| `machine_name` | string | The launched machine's name, a template with `{key}` placeholders (see `list-name-placeholders` and [cli.md](cli.md#machine-name)). Default: the model's name |
| `speed` | number or `"unlimited"` | [Emulation speed](#speed) |
| `startup` | object | [Startup keyboard links](#startup) |
| `mos_image_uri` | string | [The MOS ROM](#mos_image_uri) |
| `storage` | object | [Disc controller and floppy drives](#storage) |
| `sideways_bank` | object | [Sideways ROM and RAM slots](#sideways-romram-bank) |
| `econet` | object | [Econet station and transport](#econet) |
| `extensions` | array | [Peripheral and coprocessor extensions](#extensions) |
| `thumbnail_capture_delay_seconds` | number | How long `capture-screenshot` runs the machine before capturing the preset's thumbnail |
| `auto_boot` | bool | The older place for `startup.auto_boot`: still read, with a warning, never written |

The keys are listed once, in `kPresetKeys` (`PresetLoader.hpp`), in the order
above: the loader reads them in that order, `create-preset` and
`resolve-preset` write them in it, and `describe-preset-schema` gives its
sections in it. Any other top-level key is ignored with a warning. A `null` value means the
same as leaving the key out, so a `resolve-preset` document, which writes
`null` for "nothing here", loads as it reads.

## Paths

A preset refers to files in four places: floppy images, sideways ROM and RAM
images, the MOS, and extension parameters whose manifest type is `filepath`
(a hard-disc image, a coprocessor ROM, AUN's `map-file`). They follow one
rule, wherever the preset was named from:

1. **An absolute path** (and on Windows a path beginning `/`) is used as it is.
   So is a URI (`file:///...`).
2. **A relative path with a directory component** (`discs/game.ssd`,
   `../roms/forth.rom`) is resolved against the directory holding the preset.
3. **A bare name** (no directory component) is resolved by the resource's own
   search: a ROM name on the ROM search path ([deployment.md](deployment.md#rom-discovery-algorithm)),
   a hard-disc name among the bundled disc masters with a per-user working
   copy ([deployment.md](deployment.md#bundled-discs-and-copy-on-write)), a
   coprocessor ROM in its plugin. A bare floppy name is the exception: it is
   resolved against the preset's directory, as in rule 2. A bare name is also
   how a keyword such as AUN's `map-file: "none"` is written.

Floppy images are stored as `file://` URIs once resolved. ROM and extension
paths stay paths.

The longer-term rule, in which a bare name is looked for beside the preset
first, is set out in
[presets-and-machine-directories.md](discussion/presets-and-machine-directories.md#2-path-resolution-one-rule-for-all-three)
(#135).

## Machine settings

### speed

A positive multiple of real time (`1.0` is real time, `2` twice as fast), or
the string `"unlimited"` to run flat out. Default `1.0`. The same values as
`--speed`. Zero, a negative number or any other string is an error.

### startup

The keyboard links the MOS reads at power-up, in one of two forms:

```json
"startup": { "screen_mode": 3, "auto_boot": true }
"startup": { "links": 135 }
```

| Key | Type | Meaning |
|-----|------|---------|
| `screen_mode` | integer 0-7 | The screen mode at power-up. Default 7 |
| `auto_boot` | bool | Reverse the boot action: a reset or BREAK boots the disc's `!BOOT` and SHIFT+reset or SHIFT+BREAK does not. Default `false` |
| `links` | integer 0-255 | The whole links byte. Cannot be combined with `screen_mode` or `auto_boot` |

These are the preset forms of `--screen-mode`, `--auto-boot` and `--links`. A
top-level `auto_boot` is read as `startup.auto_boot` with a warning; giving both
a top-level `auto_boot` and a `startup` that sets the links is an error.

`links` is the byte the machine reads: the eight start-up option links on the
keyboard circuit board (SW2 in the Advanced User Guide's Appendix J), Link 1
being bit 7 and Link 8 bit 0, a set bit an open link and a clear bit a made
one. Bits 0-2 are the screen mode; bit 3 clear reverses the boot
action of reset and BREAK (`auto_boot`); bits 4 and 5 (links 4 and 3) set the disc drive timings, which
depend on the disc controller and filing system ROM; bit 6 is unused by the
MOS; bit 7 clear makes DNFS start in NFS rather than DFS. `screen_mode` and
`auto_boot` write their bits into 255, so `{"screen_mode": 3, "auto_boot":
true}` is the byte 243 (`&F3`); a byte converts back to them only when bits
4-7 are all set. `resolve-preset` writes whichever form the preset uses and
never converts one to the other.

### mos_image_uri

The MOS ROM, a name or path as for `--mos`. Default: the model's MOS
(`acorn-mos_1_20.rom` on a Model B). Model B+ variants take their own.

## Storage

```json
"storage": {
  "fdc_socket": { "id": "acorn-1770" },
  "floppy_drives": [
    { "drive": 0, "image_uri": "discs/elite.ssd" },
    { "drive": 1, "image_uri": null }
  ]
}
```

| Key | Type | Meaning |
|-----|------|---------|
| `fdc_socket.id` | string | The disc controller in the socket: an id from `list-fdcs` (today `acorn-1770`), or `none` for an empty socket. Only models with a disc controller socket; a model with a built-in controller (the B+ family) ignores it with a warning. Default: empty socket |
| `floppy_drives[]` | array | `{ "drive": 0 or 1, "image_uri": path, URI or null }`. `null` leaves the drive empty. Default: both empty |

Accepted image types are those `list-floppy-formats` lists.

## Sideways ROM/RAM bank

The BBC Micro has 16 sideways slots (0-15) sharing the address range
&8000-&BFFF, one paged in at a time by a write to &FE30. A model wires some
or all of them to physical sockets, possibly with one socket answering at
several slots; the Model B's four sockets, for instance, each repeat across
four slot numbers. Each model's sockets, the slots each answers at, and what
each can hold are in the `sideways_bank` section of
[`describe-preset-schema`](#describe-preset-schema); the topologies themselves
are described in [sideways-slots.md](sideways-slots.md).

```json
"sideways_bank": {
  "slots": [
    { "slot": 14, "type": "rom", "image_uri": "acorn-dfs_2_26.rom" },
    { "slot": 13, "type": "empty" },
    { "slot": 4, "type": "ram", "image_uri": "my-rom-under-test.rom", "write_protected": true }
  ]
}
```

| Key | Type | Meaning |
|-----|------|---------|
| `slot` | integer 0-15 | Required. A slot the model has; addressing a socket at any of its slots addresses the socket |
| `type` | `rom`, `ram` or `empty` | Required |
| `image_uri` | string | Required for `rom`; for `ram`, an image pre-loaded into the RAM; not allowed for `empty`. A name or path, see [Paths](#paths) |
| `write_protected` | bool | `ram` only: engage the slot's write-protect switch at power-up, where its socket has one. The switch can also be toggled at runtime (`SidewaysService.SetSlotProtection`) |

Slots a preset does not name keep the model's defaults: the language ROM
(BBC BASIC) in the model's language slot, and on some models a DFS or an
expansion board's own ROM (the Integra-B's IBOS). A preset slot on the same
socket as a default replaces it. Slot priority follows the slot number (the
highest slot with a language entry wins at reset); a preset does not set it.

The equivalent command-line option is `--sideways slot=N:type=...:image=...[:write-protect]`.

## Econet

```json
"econet": {
  "station": 80,
  "transport": { "name": "aun", "parameters": { "port": 0, "discovery": "off" } }
}
```

| Key | Type | Meaning |
|-----|------|---------|
| `station` | integer 1-254, or `"auto"` / `"auto:lo-hi"` | Required when `econet` is present. A number fits Econet with that station number. `auto` picks a free number in the range (default 1-253) at each launch, as `--station auto` does ([cli.md](cli.md#econet)) |
| `transport` | object or null | The network the Econet socket is connected to: `{ "name": <transport extension>, "parameters": {...} }`. Absent or null: Econet fitted with no network |

The transports are extensions of kind `econet-transport` (`aun`, `piconet`):
`describe-preset-schema` lists them and `describe-extension <name>` gives their
parameters. `parameters` follows the same rules as an extension's
[`config`](#extensions). The keys `econet.piconet` and `econet.aun_port`,
from an older format, are rejected with a message giving the new form.

## Extensions

```json
"extensions": [
  { "name": "acorn-scsi" },
  { "name": "scsi-hard-disc", "config": { "id": "boot", "image": "l3fs-v1_26b.dat", "scsi-id": 0 } },
  { "name": "host-serial", "config": { "mode": "device", "path": "/dev/ttyUSB0", "baud": 9600 } }
]
```

Each entry is one extension instance: `name` is the extension's canonical
name (`list-extensions`), and `config` its parameters. The parameters, their
types and defaults are the extension's manifest's, given by
`describe-extension <name>` (`--json` for every property: label, bounds,
choices and the attachment properties below; the manifest format is in
[peripheral-extension-framework.md](peripheral-extension-framework.md#extension-manifest)). Values may be JSON strings, integers, booleans,
or for a repeatable (`is_list`) parameter an array; `id` and `label` are
framework keys every extension accepts. `id` names the instance; one not
given an `id` takes its extension's name, or `name-1`, `name-2`, ... when
that is taken (never an `id` given explicitly), so the same preset always
names its instances the same way, and `resolve-preset` writes the ids out.

A preset's extension config is checked exactly as the equivalent command-line
flag is (`normalise_extension_config`): an unknown extension, an unknown
parameter, a missing required parameter, a value of the wrong type, outside
the parameter's `min`/`max` or not among its `choices`, or two parameters the
manifest says exclude each other (`excludes`, or two members of one of its
`groups` of alternatives, e.g. the RTC's `time` and `offset`) stops the launch with `CONFIG` (78), and the manifest's defaults are filled in for the
parameters left out. An empty value (`""`, or `[]` for a list) is the same as
leaving the parameter out, so the default applies. A single value for a list
parameter is a one-element list. A `filepath` parameter is resolved as in
[Paths](#paths).

### Attachment

An extension attaches to the points named in its manifest's `attaches_to`: a
connector of the machine (`1mhz-bus`, `user-port`, `tube`, `serial-port`), or
a point another extension provides (a SCSI host adapter provides `scsi`).
Before anything is loaded, the launch works out where each instance would
attach (`check_attachments`, the same rules the extension registry applies
when it builds the machine) and stops with `CONFIG` (78) if:

- a point it attaches to is neither on the machine nor provided by another
  instance (the message names the extensions that would provide it);
- a point would hold more than its maximum occupancy: one on the serial port,
  user port and Tube, seven on each SCSI bus, no limit on the 1 MHz bus
  (`list-attachment-points`; `describe-preset-schema` gives the same bounds);
- a parameter the manifest declares `provider_of` a point names no instance
  providing it. The value is the provider's framework `id`, so a preset that
  chooses between two adapters gives each one an `id`. With no value, the
  extension attaches to the last instance providing the point;
- a parameter the manifest declares `unique` has the same value as another
  instance of the extension on the same attachment.

These properties are general manifest parameter properties, shown by
`describe-extension`. SCSI discs use them: `scsi-hard-disc`'s `adapter-id` is
`provider_of` `scsi` and its `scsi-id` is `unique`, so two discs may share a
SCSI ID only on different adapters.

A manifest may also declare a `filepath` parameter `bundled` with a kind: a
bare name there refers to one of the images of that kind that ship with
Beebium. There are two kinds. `hard-disc` (`scsi-hard-disc`'s `image`): a disc
master in the server's disc directory, used through a per-user copy-on-write
working copy, never the master
([deployment.md](deployment.md#bundled-discs-and-copy-on-write)). `rom` (a
coprocessor's `rom`): one of the ROMs that extension itself ships in its
`roms/` directory; a bare name it does not ship is an error (write `./name`
for a file of that name in the working directory). `describe-preset-schema`
lists the bundled images of both kinds, each with a `display_name`: a ROM's
from its manifest `roms` entry, a disc master's from the `<name>.json` it
must have beside it (a `.dat` and `.dsc` without one are not a master). No floppy images are bundled.

A parameter may declare `choices_from` `"serial-ports"` (`host-serial`'s
`path`, Piconet's `device_path`): a UI may offer the host's serial ports,
which `list-serial-ports` lists, and the value stays free text.

A SCSI hard disc, then, is two entries: the adapter and the disc.

```json
"extensions": [
  { "name": "acorn-scsi" },
  { "name": "scsi-hard-disc", "config": { "image": "l3fs-v1_26b.dat", "scsi-id": 0 } }
]
```

## Which models accept what

All models accept the machine settings, the sideways bank, Econet and the
floppy drives. What differs:

| | Model B and board variants | Model B+ family |
|-|---------------------------|-----------------|
| `storage.fdc_socket` | Yes: socketed controller | Ignored: built-in WD1770 |
| Default DFS ROM | No | Yes |
| Sideways slots and types | Per board (see `describe-preset-schema`) | Per model |
| Attachment points | `1mhz-bus`, `user-port`, `tube`, `serial-port` | The same |

`describe-preset-schema` is the authoritative per-model answer, since it is
generated from the model's own definition.

## Command line over preset

A command-line option given with `--preset` overrides the preset. The rule in
every case is that the command line wins for what it names, and the preset
keeps the rest:

| Setting | Rule |
|---------|------|
| `--machine-name`, `--speed`, `--mos`, `--fdc`, `--station` | Replaces the preset's value |
| `--floppy d:...` | Replaces the preset's image for that drive |
| `--sideways slot=N...` | Replaces the preset's entry for that slot |
| `--screen-mode`, `--auto-boot`, `--links` | Per key within one form; an option of the other form replaces the preset's startup entirely (`--links` drops the preset's `screen_mode` and `auto_boot`, and the reverse) |
| `--aun`, `--piconet` (a transport) | The machine has one Econet socket, so a command-line transport stands in for the preset's: of the same name it merges over the preset's per typed key, of a different name it replaces it |
| any other `--<extension>` | Overrides the preset instance of the same extension whose `id` it gives, per typed key; otherwise it is added alongside the preset's |

"Per typed key" means only the parameters actually written on the command line
override; parameters the command line fills from manifest defaults do not
(#166). The details and examples are in [cli.md](cli.md#presets).

## Diagnostics

A preset that cannot be read, or whose JSON is invalid, stops a launch with
`NOINPUT` (66). One whose content is invalid stops it with `CONFIG` (78) and a
message naming the problem: among them every [attachment](#attachment)
problem, and every error an extension's own configuration check finds. Problems
that do not stop a launch are warnings: an unknown top-level key, the older
top-level `auto_boot`, an extension check's warnings (for what the launch may
still accept), and (from
`resolve-preset`) a ROM or floppy image that cannot be found, or a bare name in
a `bundled` parameter that is not a bundled image.

An extension's plugin may check its own configuration once the preset's
instances are normalised and their attachments are sound, for what its
manifest cannot say. The Acorn real-time clock refuses a start its `layout`
cannot hold: a `time` ("The clock cannot start in 2001: the Acorn original
(4-bit year in R1) register layout holds 1981-1996."), at `/config/time`; an
`offset` that would start it outside those years, at `/config/offset`; or, with
neither, the host's clock, at the entry. An offset or host time is judged by the
host clock when the check runs, so a preset that resolves cleanly today may not
in a later year. To ask, `start` and `resolve-preset` load the plugin library
of each configured extension (built-ins have no check), so a plugin that cannot
be loaded is an error at its entry here too, where before only the launch found
it.

`resolve-preset` locates each problem by a JSON pointer: an extension's
attachment problems at its entry (`/extensions/2`), or at the parameter at
fault (`/extensions/2/config/scsi-id`, `/extensions/1/config/adapter-id`); a
problem with the Econet transport under `/econet/transport`, at a parameter
under `/econet/transport/parameters/`. When two
instances clash, the later one is reported.

## resolve-preset

```bash
beebium-model-b resolve-preset <id|filepath> [--json]
```

Loads a preset through the same steps as `start --preset`, without building a
machine (it loads the configured extensions' plugin libraries, to ask their
configuration checks, but creates no extension), and prints the machine it describes as a preset document in which
every key is present:

- `description`, `release_date`, `machine_name`, `thumbnail_capture_delay_seconds`: the value or `null`
- `speed`: the value, `1.0` by default, `"unlimited"` for unlimited
- `startup`: `{ "links": n }`, or `{ "screen_mode": m, "auto_boot": b }` with the defaults filled in
- `mos_image_uri`: the model's MOS by default
- `storage.fdc_socket`: `{ "id": ... }` (`"none"` for an empty socket), or `null` on a model with a built-in controller; `storage.floppy_drives`: both drives, `image_uri` a `file://` URI or `null`
- `sideways_bank.slots`: in slot order, including the model's default ROMs as explicit slots; a `ram` slot always has `image_uri` (or `null`) and `write_protected`
- `econet`: `null`, or `{ "station": ..., "transport": null | {...} }`
- each extension and the transport: every manifest parameter, typed (integers as numbers, booleans as `true`/`false`, the rest strings, lists as arrays, `""` or `[]` where there is no value), and each extension with its `attaches_to`

Without `--json` the document goes to stdout and diagnostics to stderr. With
`--json` stdout is one object:

```json
{
  "preset": { ... } ,
  "diagnostics": [
    { "severity": "warning", "path": "/colour", "message": "unknown key 'colour' is ignored" },
    { "severity": "error", "path": "/extensions/1", "message": "preset extension 'host-serial': parameter 'baud' must be an integer, got 'fast'" }
  ]
}
```

`path` is a JSON pointer to the key at fault: `/econet/station`,
`/startup/links`, `/speed`, `/sideways_bank/slots/1/slot` (a slot's own
shape), `/sideways_bank/slots/1` (a slot the model's sockets refuse, or the
later of two that conflict), `/sideways_bank` (the bank as a whole),
`/storage/fdc_socket/id`, `/model`, an extension's entry or parameter. It is
`""` only when the file itself cannot be found or read. The exit status is 0
with no errors, 78 when the preset does not describe a valid machine for this
model, and 66 when it cannot be found or read as a JSON object. When the
preset cannot be loaded at all (66, or a 78 from its shape, such as a station
out of range) `preset` is `null`; the error's path still says where.

The document is itself a preset: saving it (with `create-preset --from`, or by
writing it to a file) and resolving it again gives the same document. A test
holds every shipped preset to that.

## describe-preset-schema

```bash
beebium-model-b describe-preset-schema
```

Prints JSON describing what a preset for this model may contain, for building
a configuration UI before a machine is launched:

```json
{
  "schema_version": 2,
  "model": { "id": "model-b", "name": "BBC Model B", "description": "..." },
  "sections": [ { "type": "speed", ... }, ... ]
}
```

There is one section per preset key, its `type` being the key:

| `type` | Contents |
|--------|----------|
| `speed` | `default`, and the `unlimited` keyword |
| `startup` | `screen_mode` and `links` with `min`, `max` and `default`, `auto_boot` with `default`, and `links.excludes`. `links` also lays out the byte: `link_made_when` (`clear`), `screen_mode_bits` (least significant first), `auto_boot_bit` with `auto_boot_true_when` (`clear`), and `bits`, one per bit from 7 down, each with `bit`, `link` (`Link 1` to `Link 8`), `label` and a one-sentence `description` |
| `mos_image_uri` | `default`: the model's MOS |
| `storage` | `builtin` hardware; `fdc_socket.options` (socketed models: `none` and each controller); `floppy_drives` with their geometry and image types |
| `sideways_bank` | `sockets` (label, `slots`, `capabilities`, `runtime_configurable`), `has_aliasing`, and `default_roms` (`slot`, `image`, `role`: `language`, `filing` or `board`) |
| `econet` | `station`: `min`, `max`, `auto`, `auto_default_range`; `transports`: the transport extensions' names |
| `extensions` | `attachment_points`: each point on this model (its own connectors, then points an extension provides) with `display_name`, `description`, `min_occupancy`, `max_occupancy` (`null` for no fixed bound; for a provided point, per provider), `provided_by` (`null` or the providing extensions) and `extensions` (the names of those that attach to it). `catalogue`: every extension that can attach to one of those points, and on a model with Econet every transport, each exactly as `describe-extension <name> --json` gives it (`name`, `display_name`, `description`, `cli_name`, `kind`, `source`, `attaches_to`, `provides`, `parameters`, `roms`). `bundled_images`: by kind, the bundled images a `bundled` parameter may name, each with `image`, `display_name`, `size_bytes` and `extension` (the extension it belongs to, or `null` for any), e.g. `{"hard-disc": [{"image": "l3fs-v1_26b.dat", "display_name": "Acorn Level 3 File Server 1.26", "size_bytes": 10002432, "extension": null}], "rom": [{"image": "acorn-tube-6502_1_10.rom", "display_name": "Acorn 6502 Tube ROM 1.10", "size_bytes": 4096, "extension": "acorn-65c02-coprocessor"}, ...]}` |

So one call gives a front-end everything it shows about extensions: what each
one is called and does, in a sentence a user understands, and what it takes.
A bundled parameter's `default` is a bare name, so the image it names, and its
`display_name`, are in `bundled_images` (a coprocessor's `rom` defaults to the
ROM it ships). The controller and image-format catalogues come from
`list-fdcs` and `list-floppy-formats`.

## Not in a preset

Server plumbing is launch state, not machine description, and is only on the
command line: `--port`, `--wait`, `--advertise`, `--allow-shutdown`,
`--machine-uuid`, `--machine-ordinal`, `--provenance-*`, `--rom-dir` and
`--extension-dir`.

Some machine options have no preset form yet: `--write-protect <group>` and
`--hide <group>` (the board-wide protection switches), `--motherboard-link`
(the B+ S13 link) and the Integra-B's `--integra-rtc`. `--language-rom` has no
key of its own; `create-preset --language-rom` records it as the model's
language slot in `sideways_bank`.
