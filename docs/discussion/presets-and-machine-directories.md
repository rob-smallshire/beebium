# Presets as archives and machine directories

Status: draft for discussion (2026-09-30). Step 3 (#136): stored presets are
`.beebium-preset` archives. The one-shot migration of the user store (section
3.1) ships in the next release and is removed in the release after it. The
bundled disc masters are gone: the Level 3 File Server preset carries its disc,
which `create-preset --from` takes into a copy and `export-preset --unpack`
writes out. Step 4 (#103b): machines can be saved -- `create-machine`,
`start --machine`, `describe-machine`, `reset-battery`, and
`SystemService.SaveMachine` / `ResetBattery` on a running machine; the
directory as built is described in [../machine-directory.md](../machine-directory.md).
Issues: #136 (presets as archives),
#103 (machine instances), #135 (disc image reference resolution), #134
(multi-select preset import). Prompted by Mark Moxon's Elite-over-Econet
preset pack (https://stardot.org.uk/forums/viewtopic.php?p=493669#p493669),
which needs a preset, its hard-disc images and its floppies to travel
together and install without editing.

## 1. Vocabulary

Three things, kept distinct:

| Term | What it is | Mutability | Lives |
|------|------------|------------|-------|
| **Preset** | A template for a machine: model, ROMs, storage, Econet, extensions. Stored as one zip file, `<id>.beebium-preset`, holding `preset.json` and the resources it references (hard-disc images, floppy images, ROM images, a thumbnail). | Immutable. Authored as files, stored as an archive. | System presets directory, or the user presets directory. |
| **Machine directory** | The backing store of one machine instance, `<name>.beebium-machine/`: its identity, its configuration, its working media, its battery-backed state. An extracted preset plus state. | Mutable; owned by exactly one running server at a time. | A temporary directory (transient machine) or wherever the user saved it. |
| **Machine** | A running server process on a machine directory. | Live. | A process. |

**A machine directory is not a saved machine state.** It holds exactly
what a real machine keeps when powered off: its configuration, its media,
and its non-volatile memory (battery-backed RAM, CMOS, the RTC offset).
Opening it is a power-on of that machine. It never contains volatile RAM,
CPU registers, device registers or anything else that a power cycle
discards. A **snapshot** of the running state for later resumption (#107)
is a different animal with a different suffix, `.beebium-snapshot`, and is
out of scope here; a snapshot would reference the machine directory it
was taken from, not replace it.

The relationships:

```
preset (zip)  --extract-->  machine directory  --launch-->  machine
 (template)                    (instance)                  (process)
```

Instantiation extracts. A preset is never written to by a machine;
a machine directory is never shared by two presets. This is the rule that
removes the copy-on-write tension described in #103: today a preset's
bundled hard disc gets a per-user working copy keyed by image name, so a
preset behaves like an instance. Under this design the working copy lives
in the machine directory, and a preset is only ever a template.

## 2. Path resolution: one rule for all three

Every reference to a resource (`image`, `image_uri`, ROM names) uses the
same rule, wherever it appears:

1. **A bare name** (no directory component) is looked up on a search path:
   the archive or directory containing the referencing file (so a preset's
   own resources win), then the per-user resource directory for that kind of
   resource (`discs/`, `roms/`), then the system resource directory
   (`share/beebium/discs`, `share/beebium/roms`, or the build-tree
   equivalents).
2. **A relative path with a directory component** resolves against the
   container of the file containing it: the archive for a stored preset,
   the directory for a preset being authored, the machine directory for a
   machine's configuration.
3. **An absolute path** is used as is.
4. **A leading `~/`** expands to the user's home directory on every
   platform before rules 2 and 3.

Floppy and cassette `image_uri` already follow rule 2 (`normalize_image_uri`
in `PresetLoader.hpp`). Extension `config` values do not, despite a comment
saying they do, and hard-disc bare names only search the system directory.
Making all references follow this one rule is #135, and it is a
prerequisite for both preset archives and machine directories, because
both rely on "relative to the file that says so".

What the rule does NOT decide is whether a resolved file is opened in place
or copied. That is decided by who owns it (section 4).

## 3. Presets as archives (#136)

### 3.1 Stored form: a preset is a zip file

Every stored preset, system or user, is a single zip file with the suffix
`.beebium-preset`, holding one `preset.json` and whatever resources the
preset references. There is no bare-file and no directory stored form.

```
elite-client-80.beebium-preset        (a zip archive)
  preset.json                        the one preset
  thumbnail.png
  elite.ssd
  extensions/<id>/                   data owned by one extension instance
  README.md
```

Only `preset.json` is required and only `preset.json`, `thumbnail.png` and
`extensions/` have meaning to the core. Everything else in the archive is
the author's, referenced from `preset.json` by relative path or bare name
and carried into the machine directory unchanged.

The preset's id is the file name without the suffix. The archive carries
the type, so the inner file is plain JSON with a plain name, which editors
highlight and users recognise.

**Why a zip and not a directory.** A preset exists to be instantiated, and
instantiation is a copy: extracting the archive into the machine directory
IS the instantiation (section 4.1). A single file downloads, attaches and
drops into the presets folder as one thing and cannot lose its thumbnail or
a disc image in transit. It is immutable in use by construction, not by
convention. And a file with a suffix is an ordinary document type on every
platform, where a directory would need a macOS package UTI and has no
equivalent on Windows or Linux. Today's `<id>.preset.beebium` plus
`<id>.thumbnail.png` sidecar is already two files held together by naming
convention; the archive makes that grouping real.

**Input forms.** Authoring and hand-editing happen on files, so the loader
and the importer accept three inputs: a `.beebium-preset` zip, a directory
laid out like the archive, or a lone `preset.json`. `--preset <path>` loads
any of them in place; `import-preset <path>` stores a zip in the user
presets directory, archiving a directory or wrapping a lone file.
`export-preset` writes the zip; `export-preset --unpack` writes the
directory for editing. Only zips live in the store.

**Migration (one-shot, breaking).** Presets are pre-1.0 and Beebium ships
no backward compatibility, so the change is made once: CMake generates
system presets through `create-preset`, which now writes zips, and on
first listing of the user presets directory each `<id>.preset.beebium` is
archived to `<id>.beebium-preset` with its thumbnail inside. The migration
ships in one release and is then removed.

**Disc images compress.** A hard-disc image is mostly empty: the L3FS
master is about 20 MB on disc and a few hundred kilobytes zipped, which is
why the shipped masters are committed as `.tar.xz`. So a preset can carry
its hard disc inside the archive at little cost to download size or to the
repository, and the separate bundled-masters mechanism in `DiscPaths`
becomes unnecessary once system presets are archives: the L3FS preset
simply contains its image. The 20 MB is paid only on extraction, once per
machine, where it is needed.

**Implementation note.** The server needs a small zip reader and writer,
vendored under `third_party/`. ROM images are read straight from the
archive into memory; disc images are extracted into the machine directory,
which is the copy that would have happened anyway.

**Machine directories stay directories** (section 4): they are live, with
images written continuously, a lock and state written at shutdown. A
preset is a zipped machine template; a machine is its extracted, live copy.
The `.beebium-machine` suffix is chosen now so that registering it as a
macOS document package later needs no rename.

### 3.2 Resources and resolution

A preset references its resources by bare name or by archive-relative
path, and rule 1 or 2 of section 2 finds them. No new grammar is needed.

**Why one preset per archive.** An archive holding several presets would
be a third kind of container between preset and machine, with its own
lifecycle: removing it removes several presets at once, replacing it
replaces presets the user may rely on separately, and its presets need ids
qualified by the container's name. One preset per archive keeps ids,
names, thumbnails and files one-to-one with presets, and makes a preset the
exact template of a machine directory (section 4): a machine directory is
an extracted preset plus mutable state.

**Packs.** Mark's Elite-over-Econet pack is three presets sharing two disc
images. It ships as three `.beebium-preset` files, imported together (the
macOS Import panel accepts several, #134) or dropped into the user presets
directory. The shared images are either duplicated into each archive
(simple, costs disc space, compresses well) or placed once in the per-user
`discs/` directory and referenced by bare name (rule 1). A pack is a
convention, not a concept: no manifest, no pack-level removal.

**Import.** `import-preset <path>` stores the archive in the user presets
directory under its own name; importing a name that already exists asks to
replace or to import under a suffixed name (the existing id uniqueness
rule). Replacing a preset cannot damage any machine, because machines hold
their own extracted copies (section 4.1). `list-presets` is unchanged in
output.

## 4. Machine directories (#103)

Every machine, transient or saved, runs on a machine directory. This section
takes the refinement in #103 and makes it concrete enough to build.

```
Elite Server.beebium-machine/
  machine.json          identity: uuid, name, model, created, source preset
  preset.json           the machine's own configuration (the preset, extracted
                        at instantiation, editable thereafter; this is what
                        the server actually loads; same name as inside the
                        archive, so a machine directory IS an extracted preset
                        plus state)
  thumbnail.png         last screen, captured on Save and graceful shutdown
  nvram/                non-volatile state of core hardware a real machine
                        keeps when powered off: board RAM, CMOS, RTC offset,
                        one file per hardware unit (never volatile RAM or
                        CPU state)
  extensions/<id>/      data owned by one extension instance (section 4.9)
  lock                  held by the running server (section 4.4)
  Elite Server.beebium-machine
                        the openable document file on Windows and Linux: a
                        small text file naming its own directory (section 4.11)
  desktop.ini, .directory
                        platform folder metadata: icon and display name
                        (section 4.11); written by the server, never read
  ...                   everything else the preset archive contained, at the
                        same relative paths (disc images, ROM images, notes)
```

The core defines the meaning of `machine.json`, `preset.json`,
`thumbnail.png`, `nvram/`, `extensions/`, `lock`, the document file and the
two folder-metadata files, and nothing else. It
does not prescribe a `discs/` or `roms/` layout: a preset author decides
where resources sit in the archive, extraction preserves that, and the
path rule of section 2 finds them. A directory is not a schema.

### 4.1 Instantiation

`start --preset <id|path>` (as now) creates a transient machine directory
under the per-user `machines/` area (or the platform temp directory),
extracts the preset into it, pins an instance id in `preset.json` for
every extension instance that has none (ids are otherwise generated fresh
on each launch, which a persistent directory cannot be keyed by; pinned
ids follow the existing scheme, `<name>`, `<name>-1`, unless the author
named one), and launches on
that directory.

What the machine then owns, and what it only refers to:

- **Everything in the archive** is extracted, so every disc image, ROM
  image or data file a preset ships is the machine's own copy. The guest
  writing to a shipped floppy or hard disc changes the machine, never the
  preset.
- **Resources resolved outside the archive** by bare name (section 2) are
  treated by kind: a disc image is copied into the machine directory,
  because the guest writes to it and the template rule applies; a ROM
  image is opened read-only from where it resolves and never copied.
- **Media the user inserts at runtime** (Insert Disc from the app, or the
  gRPC disc service): opened in place, as today. The user chose a file of
  their own, and expects writes to land in it.

This keeps today's behaviour for user-owned media and applies the template
rule only to media a template names. Copying a bare-name disc image into
the machine directory is the same copy-then-rename `DiscPaths` already
does; only the destination changes from a per-user directory keyed by
image name to the machine directory.

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

- `nvram/`: on graceful shutdown, and on Save. Atomic write-and-rename per
  file. A crash loses changes since the last write, which #103 accepts. A
  periodic write is an optional later improvement and needs no format change.
- Media files: continuously, as the guest writes (the existing disc
  write-back behaviour), because these are the machine's own files.
- `machine.json`: on rename (`SetMachineName` already exists) and on Save.

Each hardware unit with battery-backed state (Integra-B board RAM and
CDP6818, ATPL and Watford board RAM where fitted with a battery, a future
Master 128 CMOS) exposes a serialise/restore pair that names its file and
its layout version; the server calls them all. A restore whose layout
version or hardware configuration no longer matches (IBOS 1.20 versus 1.26,
RAM fitted to a different socket pair) is refused with a warning and the
unit starts from its seed, and the stale file is kept under `nvram/stale/`
so nothing is silently discarded. This answers questions 5, 6 and 10 in
#103: no attempt at faithfully replaying stale data across a hardware
change; the seed is the "kill memory" jumper.

`reset-battery` (CLI subcommand on a machine directory that is not running;
a gRPC action and a menu item on a running machine) deletes `nvram/` and
returns every unit to its seed. That is question 7.

### 4.4 Concurrency and determinism

- A running server holds an exclusive lock file in its machine directory. A
  second `start --machine` on a locked directory fails with a clear message
  naming the holder's PID. No shared opens (question 9).
- Tests, `capture-screenshot`, `create-preset` and thumbnail generation run
  transient machines, whose directories are made under the per-user
  `machines/` area, and always start from the seed. They never read a saved
  directory unless a test asks for one explicitly (question 8). There is no
  separate work-directory variable: a test isolates its machines by pinning
  `BEEBIUM_USER_DIRPATH` (decided at step 4).
- The lock is taken while the machine is assembled, before the server
  handles a request to stop. A process stopped in between leaves its
  transient directory to the next launch's sweep, as a crash does.

### 4.5 Identity and naming

`machine.json` carries the existing `MachineIdentity` UUID, so a saved
machine keeps its UUID across opens.

*Decided at step 4 (2026-10-17), revising question 1:* **a document's name is
its directory's name, and a machine's title is never a template.** A machine
has a plain **name** and a templated **description**:

- The name is what titles and the mDNS announcement show. A saved machine's
  is its document name, its directory's name without the suffix, derived when
  it is opened and after every Save and Save As and never stored, so a Finder
  rename shows at its next open and two machines forked by Save As show
  different titles. `SetMachineName` refuses a saved machine ("rename the
  document"). A transient machine's name is a plain `--machine-name`, else the
  preset's name and the launcher's ordinal ("Model B (Disc) #2"), and
  `SetMachineName` renames it; Save proposes it as the document name.
- The description is the #153 template (`machine_description` in a preset,
  `description` in `machine.json`), rendered live and shown as a subtitle;
  absent means no subtitle.

Why: the title was first a template, then a template that gave way to the
document's name unless it placed `{document}` itself. In use, that left two
ways to rename a saved machine -- edit the template, or rename the document --
which interacted in ways too hard to use correctly: a rename in the app did
not change the title, and a document renamed in the Finder did, or did not,
depending on the template. One plain name with one way to change it, and the
live state moved to a description beside it, keeps both features without
the interaction. See [cli.md](../cli.md#machine-name-and-description).

The `source preset` field records which preset the machine
was instantiated from, as information only. A later change to the preset
does not touch the machine (question 2).

**Descriptions as templates (#153).** A machine's description may contain
placeholders the server expands from machine state (`{econet-station}`,
`{machine-model}`, ...). `machine.json` stores the template; the rendering is
shown beside the name and follows the hardware (a station change shows at
the next Break). A preset's `machine_description` is a template too; its
picker title stays literal. (#153 first made the name itself the template;
see the decision above.)

### 4.6 Per-user layout after this design

```
~/Library/Application Support/Beebium/       (%APPDATA%\Beebium, ~/.config/beebium)
  presets/                user presets, one .beebium-preset zip each
    my-preset.beebium-preset
    elite-client-80.beebium-preset
  discs/                  user-supplied disc images referenced by bare name
  roms/                   user-supplied ROM images referenced by bare name
  machines/               transient machine directories (cleaned at exit)
```

Saved machines live wherever the user put them (section 4.10); the app
remembers recent ones (Welcome window, File > Open Recent). The macOS app
can register the `.beebium-machine` directory as a document package later;
the preset archive is an ordinary document type and needs nothing.

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
- **Presets**: what the window shows today, system and user presets, plus
  the New Machine... button into the custom builder.

Behaviour:

- **On launch** the window opens on Machines when the recents list is
  non-empty, otherwise on Presets. A returning user sees yesterday's
  machine first; a new user sees the templates.
- **File > New...** (Cmd-N) opens the window on Presets. "New" keeps meaning
  "a new machine from a template or a custom configuration"; it never lists
  instances.
- **File > Open...** (Cmd-O) is a plain open panel filtered to machine
  directories (`.beebium-machine`; a document package once registered). It
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

### 4.9 Extension-owned data

The core must not limit what a machine directory contains, because it does
not know what every extension needs. A 256 KB battery-backed paged RAM
board on the 1 MHz bus, implemented as an extension, has non-volatile
contents to keep exactly as the Integra-B does, and the core has no idea
of its layout.

So each extension instance owns a subdirectory, `extensions/<instance id>/`,
in both the preset archive and the machine directory. Instance ids keep
the scheme already in force (decided 2026-10-17): the extension name for
the first instance, then `<name>-1`, `<name>-2` for further instances of
the same extension (two SCSI discs on one adapter), or a name the author
chose. The id already carries the extension name, so the directory is
readable as it stands: `extensions/scsi-hard-disc/`,
`extensions/scsi-hard-disc-1/`, `extensions/boot-disc/`. Ids an author
does not choose are pinned at instantiation so that a persistent directory
can be keyed by them.

- **In a preset**, it holds whatever the author ships for that instance
  (a pre-loaded RAM image, a configuration file). The core extracts it
  with the rest of the archive and never reads it.
- **In a machine directory**, the framework hands the instance its
  directory path at initialisation (a `persistent_dirpath` alongside the
  existing config map), created on first use. The instance reads and
  writes there as it likes, subject to the same rules as `nvram/`: write
  atomically, write on graceful shutdown and on Save, keep a layout version
  of its own and refuse (keeping the stale file) rather than misread.
- **`reset-battery`** applies to extensions too: the framework asks each
  instance to reset its persistent data, and an instance that has none
  does nothing.

As built (step 4, R4; approved 2026-10-17): an extension keeps its state in
the shape the core's own hardware does, not in a free-form directory. It opts
in with the `ExtensionPersistence` capability and gives `BatteryBackedUnit`s,
which the server keeps through the same store as `nvram/`, in
`extensions/<id>/nvram/<unit>.bin`. Layout and hardware checks, `stale/`, and
`reset-battery` (which then needs no plugin loaded) come with it; the rest of
`extensions/<id>/` stays the instance's, for files a preset ships. A
free-form directory can be added behind the same accessor if a plugin ever
needs one.

Instance ids are pinned in the machine's `preset.json` at instantiation
(section 4.1), so an instance finds the same directory on every open. A
preset author who ships `extensions/<id>/` data pins the id in the
preset so the shipped directory matches.

Core-owned non-volatile state stays in `nvram/` rather than moving under
`extensions/`, because the core hardware units (an Integra-B board, a
Master 128 CMOS) are not extension instances and have no instance id.
The same serialise/restore surface serves both.

### 4.10 Where saved machines live (decided 2026-10-16)

A saved machine is a document, and goes wherever the user chooses. Nothing
in the design depends on its location: identity is in `machine.json`, not
the path, so a directory moved or renamed afterwards still opens, and the
recents list behind the Machines page tolerates moves. The server's only
requirements of a destination are that it is writable and does not already
contain a machine directory.

**The default for a first save is the platform documents directory**, with
no Beebium subfolder. After that the system save panel remembers the last
folder used, as it does for any document.

| Platform | First-save default | How the front-end finds it |
|----------|--------------------|----------------------------|
| macOS | `~/Documents` | `FileManager.url(for: .documentDirectory, ...)`; NSSavePanel remembers the last folder by itself |
| Windows | the Documents known folder | `SHGetKnownFolderPath(FOLDERID_Documents)`, which follows folder redirection and OneDrive-backed Documents |
| Linux | `xdg-user-dir DOCUMENTS`, falling back to `~/Documents` | `XDG_DOCUMENTS_DIR` in `user-dirs.dirs` |

The default is a front-end concern. The server never chooses a location:
`start --machine <dir>`, `create-machine --output <dir>` and
`SystemService.SaveMachine(destination)` all take an explicit path, so the
server needs no per-platform folder lookup beyond the Application Support
directory it already uses.

Two things that must not go under Documents:

- **Transient machine directories.** They are working state, not
  documents. They stay under the per-user application area (Application
  Support on macOS, `%LOCALAPPDATA%` on Windows, `$XDG_STATE_HOME` or
  `~/.local/state` on Linux) so that they are kept out of cloud sync,
  backups and indexing. (Which of Application Support or the temp
  directory is still open; see section 7.)
- **A server-chosen default.** See above.

**Cloud-synced folders.** On macOS and Windows the documents directory is
often iCloud Drive or OneDrive. An open saved machine is a directory with
continuously written disc images and a lock file, which invites sync
conflicts, and iCloud can evict a file the server has not opened yet. The
app does not refuse such locations, because refusing Documents would defeat
the default. Instead: the server opens every media file at launch rather
than lazily, writes atomically as section 4.3 requires, and the front-end
may warn once when a machine is saved into a folder it recognises as
sync-managed. The exact behaviour under sync is settled in the Save/Open
step of the sequencing (section 6, step 3), not by the location choice.

### 4.11 A machine directory as a document on every platform (decided 2026-10-16)

Only macOS can present a directory as a single document: a document
package, a directory whose extension maps to a UTI conforming to
`com.apple.package`, is shown by Finder as one item with the app's icon and
opens in the app on double-click. Windows Explorer always treats a
directory as a folder whatever its name, and the Linux file managers have
no package concept. Both can give a folder a custom icon (`desktop.ini`
with `IconResource` plus the folder's read-only attribute; a `.directory`
file with `Icon=`) and Windows and KDE can override the displayed name
(`LocalizedResourceName`, `Name=`), but neither can make double-clicking a
folder open an application short of a shell extension, which is out of the
question.

**Decision: the project-folder pattern.** The machine stays a directory,
identical on every platform, and carries one small registered document
file inside it, `<name>.beebium-machine`, the same name as the directory.
This is the shape of a `.sln` or `.uproject`: the folder is the project,
the file inside is what you double-click.

- **On Windows and Linux** the app associates the `.beebium-machine` file
  extension with itself. Double-clicking the file, or dropping it or its
  folder on the app, opens the machine. The Open panel accepts either the
  file or the directory. The folder shows the Beebium machine icon and its
  display name without the suffix where the desktop supports it.
- **On macOS** the app registers the `.beebium-machine` directory as a
  document package. Finder shows one document with the app's icon, hides
  the contents, and double-click opens the machine. The inner file is still
  written, so a machine copied to a PC is complete; it is simply invisible
  inside the package.
- **The on-disk layout is the same everywhere**, so a saved machine moves
  between platforms unchanged. The two alternatives rejected were a
  platform-specific layout (a packaged directory on macOS, a plain folder
  elsewhere), which breaks that, and a single-file zip document for saved
  machines, which would be uniform but turns live disc writes into
  write-back-on-save and loses writes on a crash; it stays in reserve
  (section 7) if the directory model proves awkward.

**The document file.** A small text file whose only job is to be
double-clickable and to identify its directory. It carries the format
name and version and the machine UUID (which lets the app check that the
file has not been separated from its directory), nothing the server reads
to run the machine. `machine.json` remains the identity record.

**Folder metadata.** The server writes `desktop.ini` and `.directory` on
Save, and sets the read-only attribute on the directory on Windows (which
Explorer requires before it honours `desktop.ini`; it does not make the
contents read-only). The icon they reference is the installed app's
machine-document icon on Windows and a named icon the Linux packaging
installs into the hicolor theme. The server never reads either file, and
neither is required: a machine directory with both deleted still opens.
`machine.json` holds the name; the two metadata files are rewritten from it
on every Save and on rename, so a display name can never go stale.

**Opening a machine, all platforms.** The front-end receives a path from a
double-click, a drop, the Open panel or the recents list. If the path is
the document file, its parent is the machine directory; if it is a
directory, it is used as is. The front-end then launches a server with
`start --machine <dir>`. The server identifies a machine directory by
`machine.json`, never by the document file or the suffix, so a renamed or
suffix-stripped directory still opens from the command line.

## 5. CLI surface

| Command | Meaning |
|---------|---------|
| `start --preset <id\|file>` | Instantiate a transient machine and run it (as now). |
| `start --machine <dir>` | Open a saved machine directory and run it. |
| `create-machine --preset <id\|file> --output <dir>` | Instantiate without running (scripts, tests, packaging a ready-made machine). |
| `import-preset <path>` | Import a `.beebium-preset` archive, a directory laid out like one, or a lone `preset.json`; the store holds the archive. |
| `export-preset <id> --output <path> [--unpack]` | Write the archive, or the directory form for editing. |
| `reset-battery <dir>` | Delete `nvram/` of a machine that is not running. |
| `SystemService.SaveMachine`, `ResetBattery` (gRPC) | The same on a running machine; the app's File > Save... and a menu action. |

Pre-launch configuration stays on the CLI; only operations on a running
machine are RPCs, following the existing rule.

## 6. Sequencing (revised 2026-10-17)

1. **#135, path rule** (server): the one resolution rule of section 2 for
   every reference, tilde expansion, and the per-user `discs/` and `roms/`
   directories on the bare-name search path. Small, self-contained, and
   enough on its own to make Mark's pack install by drag-and-drop into two
   folders.
2. **#103a, transient machine directories** (server): every launch
   instantiates its preset into a per-machine directory under the per-user
   `machines/` area, the copy rules of 4.1 apply, the lock lands, and the
   per-preset working copy keyed by image name in `DiscPaths` is retired
   (with the migration offer of 4.7 deferred to step 4, when there is a
   saved form to offer). No proto change. Scope decided 2026-10-17: the
   step 2 machine directory holds only the copied media and the lock;
   `preset.json`, `machine.json`, pinned instance ids and
   `extensions/<id>/` arrive with extraction (step 3) and Save/Open
   (step 4), since nothing reads them before then. Copy rule: media a
   template names by search or by position within itself (a system
   master, a per-user `discs/` file, a file beside the preset) becomes
   the machine's own copy; media the user names by location (an absolute
   or `~/` path, a command-line file in the working directory, media
   inserted at runtime) is the user's own file, opened in place. From
   this step the L3FS disc no longer persists between launches until
   Save exists, so master is not released between steps 2 and 4.
3. **#136, preset archives** (server, then macOS): the zip stored form
   with its vendored zip library and one-shot migration, `create-preset`
   writing zips for the build, the three input forms, `export-preset
   --unpack`, multi-select Import panel (#134). Extraction into a machine
   directory already exists after step 2, so a zip is simply its input and
   writable media inside an archive need no special handling.
4. **#103b, saved machines** (server, then macOS): Save/Open with the
   document file and folder metadata of 4.11, the battery-state
   serialise/restore surface with the Integra-B as the first unit,
   `reset-battery`, the Machines page of the Welcome window, and the two
   RPCs. The only step that moves the protocol fingerprint.

Step 4 rulings (2026-10-17), taken on the implementation plans:

- Order: one machine identity, the machine home lifted out of assembly,
  the battery unit seam, then the directory files, the nvram store and the
  CLI (`create-machine`, `start --machine`, `reset-battery`,
  `describe-machine`), and only then the media re-pointing (extension ABI
  6) and the RPCs. The protocol fingerprint moves once, in the RPC commit.
  After the CLI commit a saved L3FS machine persists by command line, so a
  release could be cut there if wanted; that is the user's decision.
- Save of a running transient machine quiesces, flushes, copies the
  directory, re-points every template medium, moves the lock, removes the
  old directory and resumes. It never cold-boots the machine.
- Save As on a saved machine duplicates it into the new directory with a
  new UUID and the running machine continues on the new directory.
- The migration offer of 4.7 is dropped (pre-1.0, no backward
  compatibility); the old per-user `discs/` working copy is left in place
  and the setup guide gives the `create-machine` recipe for reusing it.
- The "written to template media" signal of 4.7 ships with the RPCs as
  one boolean on the existing server status event
  (`template_media_written`), so the fingerprint does not move again for
  it. The macOS prompt built on it may follow later.
- `SystemInfo` gains `machine_dirpath` and `machine_saved`; the front-end
  chooses Save against Save As from them.
- The front-end never reads `machine.json` itself: `describe-machine`
  reports name, UUID, model, preset, times, the thumbnail path and the
  locking process, and the Machines page reads the thumbnail by that path.
- `create-preset --from <running machine>` (section 7) stays out of
  step 4. No `BEEBIUM_MACHINE_WORK_DIR`: tests pin `BEEBIUM_USER_DIRPATH`.

Step 2 was moved ahead of the archives so that writable media shipped in
an archive never pass through the legacy per-user working copy, where two
presets shipping an image of the same name would collide and the code
would be discarded one step later.

## 7. Open questions

- Should a preset be allowed to reference a resource outside its archive
  (a bare ROM name that resolves to the system ROMs, yes; a relative path
  climbing out of the archive, no)?
- (Resolved 2026-10-17) Transient machine directories live under the
  per-user `machines/<uuid>/`, removed on clean exit; a directory left by
  a crash is swept at the next start once its lock can be taken. Crash
  recovery as an offer to save is a later refinement.
- Should Save be allowed while the machine is running, or only when paused?
  A hard-disc image copied mid-write is not consistent; the server can
  quiesce the emulation for the copy, which the existing pause primitive
  supports.
- The single-file zip form for saved machines (rejected in 4.11 in favour
  of the project-folder pattern) is kept in reserve: it would unify presets
  and machines into one archive format at the cost of write-back-on-save
  semantics for disc images.
- Does `create-preset --from <running machine>` (capture the current
  configuration of a machine back into a template) belong in this design?
  It is the inverse arrow and cheap once a machine directory is an
  extracted preset plus state: strip the state, zip the rest.
