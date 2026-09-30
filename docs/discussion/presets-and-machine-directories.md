# Presets, preset directories and machine directories

Status: draft for discussion (2026-09-30). Issues: #136 (preset directories),
#103 (machine instances), #135 (disc image reference resolution), #134
(multi-select preset import). Prompted by Mark Moxon's Elite-over-Econet
preset pack (https://stardot.org.uk/forums/viewtopic.php?p=493669#p493669),
which needs a preset, its hard-disc images and its floppies to travel
together and install without editing.

## 1. Vocabulary

Three things, kept distinct:

| Term | What it is | Mutability | Lives |
|------|------------|------------|-------|
| **Preset** | A template for a machine: model, ROMs, storage, Econet, extensions. Stored as a **preset directory**, `<id>.beebiumpreset/`, holding `preset.json` and the resources it references (hard-disc images, floppy images, ROM images, a thumbnail). | Immutable in use. Edited only as files. | System presets directory, or the user presets directory. |
| **Machine directory** | The backing store of one machine instance, `<name>.beebiummachine/`: its identity, its configuration, its working media, its battery-backed state. A preset directory plus state. | Mutable; owned by exactly one running server at a time. | A temporary directory (transient machine) or wherever the user saved it. |
| **Machine** | A running server process on a machine directory. | Live. | A process. |

A **snapshot** (#107, full CPU/RAM/device state for resumption) is a fourth
thing and is out of scope here. Opening a machine directory is a power-on of
that machine, not a resumption.

The relationships:

```
preset directory --instantiate-->  machine directory  --launch-->  machine
   (template)                        (instance)                    (process)
```

Instantiation copies. A preset directory is never written to by a machine;
a machine directory is never shared by two presets. This is the rule that
removes the copy-on-write tension described in #103: today a preset's
bundled hard disc gets a per-user working copy keyed by image name, so a
preset behaves like an instance. Under this design the working copy lives
in the machine directory, and a preset is only ever a template.

## 2. Path resolution: one rule for all three

Every reference to a resource (`image`, `image_uri`, ROM names) uses the
same rule, wherever it appears:

1. **A bare name** (no directory component) is looked up on a search path:
   the directory of the file containing the reference (so a preset
   directory's own resources win), then the per-user resource directory for that kind of
   resource (`discs/`, `roms/`), then the system resource directory
   (`share/beebium/discs`, `share/beebium/roms`, or the build-tree
   equivalents).
2. **A relative path with a directory component** resolves against the
   directory of the file containing it: the preset directory for a preset in
   one, the preset's own directory for a lone preset file, the machine
   directory for a machine's configuration.
3. **An absolute path** is used as is.
4. **A leading `~/`** expands to the user's home directory on every
   platform before rules 2 and 3.

Floppy and cassette `image_uri` already follow rule 2 (`normalize_image_uri`
in `PresetLoader.hpp`). Extension `config` values do not, despite a comment
saying they do, and hard-disc bare names only search the system directory.
Making all references follow this one rule is #135, and it is a
prerequisite for both preset directories and machine directories, because
both rely on "relative to the file that says so".

What the rule does NOT decide is whether a resolved file is opened in place
or copied. That is decided by who owns it (section 4).

## 3. Preset directories (#136)

### 3.1 Stored form: a preset is always a directory

Every stored preset, system or user, is a directory with the type suffix
`.beebiumpreset`, holding one `preset.json` and whatever resources the
preset references. There is no bare-file stored form.

```
elite-client-80.beebiumpreset/
  preset.json                      the one preset
  thumbnail.png
  elite.ssd
  README.md
```

The preset's id is the directory name without the suffix. The directory
carries the type, so the inner file is plain JSON with a plain name, which
editors highlight and users recognise. Today's `<id>.preset.beebium` plus
`<id>.thumbnail.png` sidecar is already two files held together by naming
convention; the directory makes that grouping real, and adding a README,
a screenshot or a disc image later never changes the preset's kind.

**A bare JSON file remains an input, not a stored form.** `--preset <file>`
on the command line and `import-preset <file>` accept a lone JSON file (a
preset pasted from a forum); import wraps it in a directory. Export writes
a directory. A file given on the command line is loaded in place with no
resources, which is the degenerate directory.

**Migration (one-shot, breaking).** Presets are pre-1.0 and Beebium ships
no backward compatibility, so the change is made once: CMake generates
system presets as directories, and on first listing of the user presets
directory each `<id>.preset.beebium` is moved to
`<id>.beebiumpreset/preset.json` with its thumbnail alongside. The
migration ships in one release and is then removed.

**The suffix is chosen now** so that registering the directory as a macOS
document package later (Finder shows one item, double-click imports) needs
no rename and no second migration. Linux and Windows see a directory with a
suffix, which is harmless. The machine directory follows the same rule
with `.beebiummachine` (section 4).

### 3.2 Resources and resolution

A preset references its resources by bare name or by directory-relative
path, and rule 1 or 2 of section 2 finds them. No new grammar is needed.

**Why one preset per directory.** A directory holding several presets would
be a third kind of container between preset and machine, with its own
lifecycle: removing it removes several presets at once, replacing it
replaces presets the user may rely on separately, and its presets need ids
qualified by the container's name. One preset per directory keeps ids,
names, thumbnails and packages one-to-one with presets, and makes a preset
directory the exact template of a machine directory (section 4): a machine
directory is a preset directory plus mutable state.

**Packs.** Mark's Elite-over-Econet pack is three presets sharing two disc
images. It ships as a zip of three preset directories, imported one by one
(the macOS Import panel accepts several, #134) or by dropping them into the
user presets directory. The shared images are either duplicated into each
directory (simple, costs disc space) or placed once in the per-user
`discs/` directory and referenced by bare name (rule 1). A pack is a
convention, not a concept: no manifest, no pack-level removal.

**Import.** `import-preset <dir|file>` copies the directory as a unit (or
wraps the file) into the user presets directory under its own name;
importing a name that already exists asks to replace or to import under a
suffixed name (the existing id uniqueness rule). Replacing a preset
directory cannot damage any machine, because machines hold their own
copies (section 4.1). `list-presets` is unchanged in output; a preset
directory lists like any other preset.

## 4. Machine directories (#103)

Every machine, transient or saved, runs on a machine directory. This section
takes the refinement in #103 and makes it concrete enough to build.

```
Elite Server.beebiummachine/
  machine.json          identity: uuid, name, model, created, source preset
  thumbnail.png         last screen, captured on Save and graceful shutdown
  preset.json           the machine's own configuration (the preset, copied at
                        instantiation, editable thereafter; this is what the
                        server actually loads; same name as in a preset
                        directory, so a machine directory IS a preset
                        directory plus state)
  discs/                working copies of hard-disc and floppy images the
                        configuration references
  state/                battery-backed state: board RAM, CMOS, RTC offset,
                        one file per hardware unit
  lock                  held by the running server (section 4.4)
```

### 4.1 Instantiation

`start --preset <id|file>` (as now) creates a transient machine directory
under the per-user `machines/` area (or the platform temp directory), copies
the preset (or the whole preset directory) into it, resolves every media reference the
preset makes, copies each **writable** resource into `discs/` and rewrites
the configuration to point at the copy, and launches on that directory.

Which resources are copied:

- **Hard-disc images**: always copied. The guest writes to them.
- **Floppy images referenced by the preset**: copied. A pack's
  `elite.ssd` is a template; the guest saving a commander must not modify
  the pack.
- **ROM images**: never copied; opened read-only from where they resolve.
  A sideways RAM `image_uri` preload is read once.
- **Media the user inserts at runtime** (Insert Disc from the app, or the
  gRPC disc service): opened in place, as today. The user chose a file of
  their own, and expects writes to land in it.

This keeps today's behaviour for user-owned media and applies the template
rule only to media a template names.

Copying is the same copy-then-rename `DiscPaths` already does; only the
destination changes from a per-user directory keyed by image name to the
machine directory.

### 4.2 Transient and saved

A transient machine's directory is deleted when its server exits cleanly.
**Save** (File > Save... on macOS; a `SystemService.SaveMachine(destination)`
RPC underneath, since the server owns the open files and the in-memory
battery state) writes current state and moves the directory to the chosen
location. From then on the machine is saved: its directory outlives the
process. **Open** (`start --machine <dir>`, File > Open...) launches a server
on an existing directory.

Nothing is converted between the two forms. The only differences are where
the directory lives and whether the server removes it at exit.

### 4.3 What the server writes, and when

- `state/`: on graceful shutdown, and on Save. Atomic write-and-rename per
  file. A crash loses changes since the last write, which #103 accepts. A
  periodic write is an optional later improvement and needs no format change.
- `discs/`: continuously, as the guest writes (the existing disc write-back
  behaviour), because these are the machine's own files.
- `machine.json`: on rename (`SetMachineName` already exists) and on Save.

Each hardware unit with battery-backed state (Integra-B board RAM and
CDP6818, ATPL and Watford board RAM where fitted with a battery, a future
Master 128 CMOS) exposes a serialise/restore pair that names its file and
its layout version; the server calls them all. A restore whose layout
version or hardware configuration no longer matches (IBOS 1.20 versus 1.26,
RAM fitted to a different socket pair) is refused with a warning and the
unit starts from its seed, and the stale file is kept under `state/stale/`
so nothing is silently discarded. This answers questions 5, 6 and 10 in
#103: no attempt at faithfully replaying stale data across a hardware
change; the seed is the "kill memory" jumper.

`reset-battery` (CLI subcommand on a machine directory that is not running;
a gRPC action and a menu item on a running machine) deletes `state/` and
returns every unit to its seed. That is question 7.

### 4.4 Concurrency and determinism

- A running server holds an exclusive lock file in its machine directory. A
  second `start --machine` on a locked directory fails with a clear message
  naming the holder's PID. No shared opens (question 9).
- Tests, `capture-screenshot`, `create-preset` and thumbnail generation run
  transient machines whose directories are created under
  `BEEBIUM_MACHINE_WORK_DIR` when set (the existing `BEEBIUM_DISC_WORK_DIR`
  generalised) and always start from the seed. They never read a saved
  directory unless a test asks for one explicitly (question 8).

### 4.5 Identity and naming

`machine.json` carries the existing `MachineIdentity` UUID, so a saved
machine keeps its UUID across opens, and the mutable name. The per-run
"#N" ordinals the macOS app assigns remain the default name for a transient
machine; Save proposes that name for the file. A saved machine's display name
is the name in `machine.json`, not the directory name, so renaming the
directory in Finder does not rename the machine (question 1).

The `source preset` field records which preset the machine
was instantiated from, as information only. A later change to the preset
does not touch the machine (question 2).

### 4.6 Per-user layout after this design

```
~/Library/Application Support/Beebium/       (%APPDATA%\Beebium, ~/.config/beebium)
  presets/                user presets and imported preset directories
    my-preset.beebiumpreset/
    elite-client-80.beebiumpreset/
  discs/                  user-supplied disc images referenced by bare name
  roms/                   user-supplied ROM images referenced by bare name
  machines/               transient machine directories (cleaned at exit)
```

Saved machines live wherever the user put them; the app remembers recent
ones (Welcome window, File > Open Recent). The macOS app can register the
`.beebiummachine` directory as a document package later, exactly as for
preset directories.

### 4.7 Behaviour change to flag

Today the L3FS preset's hard disc persists between launches because its
working copy is keyed by image name in the per-user `discs/` directory. A
user who has been putting files on that server relies on that. Under this
design a transient L3FS machine starts from the pristine master every time,
and the server's disc only persists if the machine is saved.

Two mitigations, both probably wanted:

- The macOS app asks "Save this machine?" on close when the machine has
  written to template media (the server can report this), the same way a
  document app asks about unsaved changes.
- On first launch after upgrade, an existing working copy in `discs/` is
  offered as a saved machine ("A Level 3 File Server disc from an earlier
  version was found. Keep it as a saved machine?"), then the legacy
  per-preset working-copy mechanism is removed.

### 4.8 Welcome window, File > New, File > Open

The Welcome window today is a preset picker: it appears on launch, and
File > New... (Cmd-N) focuses or opens it. Once machines can be saved, the
window has two audiences that map onto the template/instance split:

- **Machines**: saved machines the user can reopen, each with a preview.
  The list behind it is the standard macOS recents list
  (`NSDocumentController.shared.noteNewRecentDocumentURL` on every Save and
  Open; the app need not be `NSDocument`-based to use it), so it costs no
  registry of our own and survives moves the same way Finder aliases do. An
  Open... button on this page is the same file picker as File > Open....
- **Presets**: what the window shows today, system and user presets
  (including preset directories), plus the New Machine... button into the
  custom builder.

Behaviour:

- **On launch** the window opens on Machines when the recents list is
  non-empty, otherwise on Presets. A returning user sees yesterday's
  machine first; a new user sees the templates.
- **File > New...** (Cmd-N) opens the window on Presets. "New" keeps meaning
  "a new machine from a template or a custom configuration"; it never lists
  instances.
- **File > Open...** (Cmd-O) is a plain open panel filtered to machine
  directories (`.beebiummachine`; a document package once registered). It
  does not go through the Welcome window.
- **File > Open Recent...** is a single item that opens the Welcome window
  on the Machines page. The page shows each machine with a preview, so it
  is a better recents list than a submenu of names. The standard recents
  submenu is not used; the recents list itself (`NSDocumentController`) is
  still the store behind the page, so Clear Menu becomes a Clear button on
  the page.
- **Previews.** A machine directory carries `thumbnail.png`, captured by the
  server on Save and on graceful shutdown (the same capture path
  `capture-screenshot` and the preset thumbnails use), so the Machines page
  shows what was on screen when the machine was last closed. A preset shows
  its shipped thumbnail as today.
- **File > Save...** (Cmd-S) on a transient machine window saves it
  (section 4.2); on a saved machine it writes state now. **Save As...**
  duplicates a saved machine into a new directory with a new UUID.

The zero-or-one Welcome invariant is unchanged; the window gains a page
selector, not a second window. Launching from either page while a machine
window is open opens a new machine window, as now.

A saved machine that is already running (locked, section 4.4) is shown in
Machines with its state; choosing it brings its window forward rather than
launching a second server, matching the existing one-window-per-target rule.

## 5. CLI surface

| Command | Meaning |
|---------|---------|
| `start --preset <id\|file>` | Instantiate a transient machine and run it (as now). |
| `start --machine <dir>` | Open a saved machine directory and run it. |
| `create-machine --preset <id\|file> --output <dir>` | Instantiate without running (scripts, tests, packaging a ready-made machine). |
| `import-preset <file\|dir>` | Import a preset file or a preset directory. |
| `reset-battery <dir>` | Delete `state/` of a machine that is not running. |
| `SystemService.SaveMachine`, `ResetBattery` (gRPC) | The same on a running machine; the app's File > Save... and a menu action. |

Pre-launch configuration stays on the CLI; only operations on a running
machine are RPCs, following the existing rule.

## 6. Sequencing

1. **#135, path rule** (server): the one resolution rule of section 2 for
   every reference, tilde expansion, and the per-user `discs/` and `roms/`
   directories on the bare-name search path. Small, self-contained, and
   enough on its own to make Mark's pack install by drag-and-drop into two
   folders.
2. **#136, preset directories** (server, then macOS): the directory stored
   form with its one-shot migration, CMake generating system presets as
   directories, directory import, multi-select Import panel (#134). Writable media shipped in a preset
   directory are still handled by today's per-user working-copy mechanism
   until step 3, so such a hard disc behaves exactly like the L3FS preset's.
3. **#103, machine directories** (server, then macOS): transient directories
   and the copy rules of 4.1 first (this alone retires the per-preset working
   copy and introduces the lock), then Save/Open, then the battery-state
   serialise/restore surface with the Integra-B as the first unit.

Each step ships on its own and none needs a proto change until step 3's
Save and ResetBattery RPCs.

## 7. Open questions

- Should a preset directory be allowed to reference a resource outside
  itself (a bare ROM name that resolves to the system ROMs, yes; a relative
  path climbing out of the directory, probably no)?
- Is the transient `machines/` area under Application Support, or the
  platform temp directory? Application Support makes crash recovery
  (an orphaned directory offered for saving, like an unsaved document)
  possible; temp makes cleanup automatic.
- Should Save be allowed while the machine is running, or only when paused?
  A hard-disc image copied mid-write is not consistent; the server can
  quiesce the emulation for the copy, which the existing pause primitive
  supports.
- Does `create-preset --from <running machine>` (capture the current
  configuration of a machine back into a template) belong in this design?
  It is the inverse arrow and cheap once a machine directory is a preset
  directory plus state: strip the state, keep the rest.
