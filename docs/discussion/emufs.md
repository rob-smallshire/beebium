# EMUFS: A Host Filing System over Emulated 1 MHz Bus Hardware

## Motivation

Getting programs and data into and out of an emulated BBC Micro is
awkward. The established routes are all unsatisfying in different ways:

- **Disc images.** Authentic, but every transfer means building or
  editing an `.ssd`/`.dsd` outside the emulator. Round-tripping a source
  file you are actively editing on the host is tedious.
- **Pasting text.** Works for a few lines. Pasting a long BASIC listing
  is slow, and it only reaches programs that read the keyboard through
  the OS.
- **Typing commands on the user's behalf.** Emulators do this to
  implement "copy BASIC listing" and similar. It is fragile (see the
  prior-art survey below) and it puts the emulator in the business of
  guessing what the guest OS will echo.

EMUFS proposes the direct answer: a filing system on the Beeb that reads
and writes a directory on the host machine. `*CAT` lists host files.
`LOAD "PROG"` reads one. `SAVE "PROG"` writes one back. The host
directory is an ordinary directory the user can edit with ordinary
tools.

The distinguishing design constraint is *how* that is implemented.

## Design principle: no host interception of guest state

The emulator must not reach into the guest to make this work. Concretely,
the host side may not:

- trap on MOS entry points or filing-system vectors,
- read or write guest memory directly,
- assign the 6502 program counter or registers,
- synthesise guest code (for example, writing a `BRK` into page 1 to
  raise an error),
- perform unbounded work inside a single emulated bus cycle.

What it *may* do is emulate a peripheral: a device with registers at
documented addresses, which the guest drives by reading and writing those
registers, and whose behaviour a real piece of hardware could exhibit.

This is the same principle already applied to keyboard input (see issue
\#49: pace and observe the hardware matrix rather than poking OS
buffers). It costs more up front -- it requires real 6502 filing-system
code on the guest side -- and it buys correctness that holds outside the
BASIC prompt, in any language, in any screen mode, for any program,
including ones that bypass the OS entirely.

## Prior art

Three emulators were surveyed. They land in three different places, and
the differences are instructive.

### BeebEm: no host filing system

BeebEm has none. Host interchange is explicit import/export of files
into and out of disc images (`Src/DiscEdit.cpp`,
`Src/ImportFileDialog.cpp`, `Src/ExportFileDialog.cpp`). Its "Copy"
feature is unrelated to filing systems: it types `L.` into the keyboard
buffer and captures the resulting printer stream.

### B-Em VDFS: a real ROM, but the host drives the CPU

VDFS is the most direct analogue and it is a hybrid that cheats
substantially.

The honest parts are genuinely honest. There is a real sideways ROM
(`roms/general/vdfs.rom`, source `src/vdfs.asm`, ~1700 lines of
beebasm). It has a proper `&82` service header. It claims the seven
filing-system vectors through the documented extended-vector mechanism
(`vdfs.asm:206-243`). Host communication is through four real ports in
the FRED 1 MHz bus area at `&FC5C-&FC5F` (`vdfs.asm:47-50`). There is no
opcode hack and no trap at a MOS address.

But the ROM contains no filing-system logic whatsoever. Every vector
handler is three instructions:

```asm
.file       sta     port_a
            lda     #&01
            sta     port_cmd
            rts
```

and everything of consequence happens in `src/vdfs.c` (~5300 lines).
Four behaviours there are impossible for real hardware:

1. **Unbounded host work inside one emulated store.** `vdfs.c:5233-5236`
   runs the entire dispatched operation synchronously inside the
   emulated `STA`, so loading a 40 KB file consumes zero emulated cycles.
2. **Direct guest memory access.** The host uses `readmem`/`writemem`
   throughout rather than the guest pumping bytes through a data port.
3. **Direct PC assignment.** `rom_dispatch` (`vdfs.c:416-423`) reads a
   dispatch table out of the guest ROM at `&8000` and assigns `pc`.
   `osfsc` sets `pc = 0xfff4` to invoke OSBYTE.
4. **Synthesised guest code.** `vdfs_error` (`vdfs.c:585-596`) writes a
   `BRK` plus an error string into page 1 and jumps to `&100`.

VDFS is therefore a useful reference for *filing system semantics* --
its OSFILE/OSARGS/OSGBPB/OSFSC coverage is close to complete, and its
`.inf` handling and filename transliteration are well worked out -- but
it is not a model for the mechanism.

### BeebLink (via b2): the model

BeebLink is a real-hardware product: a filing system ROM on the Beeb, a
physical link (Tube Serial adapter, UPURS cable on the user port, or a
legacy AVR device), and a server process on the host. b2 emulates it.

The emulated device in b2 is strikingly small -- around 220 lines
including logging -- and consists of exactly two registers
(`BBCMicro.cpp:2965`):

```
$FE9E  control  read:  bit 7 set if response bytes are available
                write: 0 = presence check (device queues a signature)
                       1 = begin send, discard receive FIFO
                       2 = begin send, retain FIFO (fire-and-forget)
$FE9F  data     write: accumulate request bytes
                read:  pop next response byte
```

There is no timing model, no baud rate, no status bytes, no sync mode.
All of that exists in the real serial links and evaporates in the
emulated one. The guest runs the *unmodified* real-hardware ROM. b2's
code never touches guest PC, registers, or memory for filing-system
purposes.

The guest-side ROM is a complete filing system in real 6502: all seven
vectors (`blfs_osfile` through `blfs_osfsc`), a `&82` service header,
boot-key handling, Tube claim/release around parasite transfers. It is
deliberately thin on *policy* -- the 6502 marshals arguments into a
request and unmarshals the response; wildcard resolution, cataloguing,
metadata, and most `*` commands are implemented host-side.

This is the architecture EMUFS should follow.

## What already exists in Beebium

The extension framework is substantially ready. Nothing in the following
list needs new work:

| Capability | Where |
|---|---|
| Address-range claim with overlap detection | `extension-api/OneMHzBusPort.cpp:28-58` |
| Per-byte dispatch across FRED and JIM | `extension/OneMHzBusPort.hpp:35-88` (512-entry table) |
| 1 MHz bus stretching | `BusStretching.hpp:62-92` -- automatic, address-based |
| IRQ aggregation from bus devices | `ModelBHardware.hpp:138`, `IrqBinding<OneMHzBusPort, 3>` |
| Per-1 MHz-cycle `tick()` | `ModelBHardware.hpp:449` |
| `1mhz-bus` attachment point, occupancy `0..N` | `ServerMain.hpp:1795` |
| Plugin RPC without gRPC symbols crossing the boundary | `extension/ExtensionRpc.hpp` |
| Publication as a storage device to clients | `extension/ExtensionStorage.hpp:31-64` |
| Manifest parameters, including a `filepath` type | `PeripheralExtensionService.ListExtensions` |

Two existing extensions are direct templates:

- **`acorn-scsi`** (`AcornScsiHostAdapter.hpp:45-123`) claims FRED
  `&FC40-&FC43` and, with `scsi-hard-disc`, fronts host-file storage from
  a 1 MHz bus device. This is the closest existing analogue to EMUFS.
- **`test-scratch-ram`** (`TestScratchRam.cpp:31-56`) is a ~56-line
  minimal reference: 8 bytes at `&FC80`, plus an RPC dispatcher.

Note that FRED and JIM are a single 512-byte region in the memory map
(`ModelBHardware.hpp:903`), so a device claims offsets `0x000-0x0FF` for
FRED and `0x100-0x1FF` for JIM. There is no separate JIM decode.

## Gaps that EMUFS must fill

### 1. The filing system ROM

This is the substantial new ground. The repository has never assembled a
ROM image from source and has no CMake rule for doing so. beebasm is used
only at *test runtime*, to build bootable discs for integration tests
(`tests/test_serial_break_e2e.cpp:93-113`, documented in
`docs/testing-from-disc.md`). All shipped ROMs are prebuilt binaries in
`roms/`.

### 2. ROM installation ergonomics

There is no extension API for supplying a sideways ROM. The precedent
(`acorn-65c02-coprocessor/CMakeLists.txt:38-42`) globs `roms/*.rom` and
`configure_file`s them into `${CMAKE_BINARY_DIR}/roms/`, leaving the user
to pass `--sideways <slot>:rom:<image>` separately. So an extension
supplies the hardware while the user must independently supply the ROM
that drives it. For a coprocessor that is tolerable; for EMUFS, where the
ROM and the device are useless apart, it is a poor experience. Closing
this gap -- letting an extension declare a sideways ROM that is installed
when the extension is attached -- is arguably a prerequisite.

### 3. Path containment

**EMUFS would be the first component in Beebium where the guest can name
a host path.** A Beeb-side equivalent of `LOAD "../../../etc/passwd"`
must not escape the configured root.

No convention for this exists. Extension file paths are entirely
unconstrained today: `scsi-hard-disc` takes an `image` filepath parameter
and opens it (`ScsiHardDiscExtension.cpp:62-74`), and the only
path-containment check anywhere in the repository is for preset files
(`PresetPaths.hpp:150-153`, using `weakly_canonical` plus a prefix test).

Note also the deliberate existing convention that extensions use
`std::filesystem::absolute()` + `lexically_normal()` rather than
`canonical`, specifically so symlinks are preserved for display
(reasoning at `ScsiHardDiscExtension.cpp:53-61`). Containment checking
needs `weakly_canonical` semantics to be sound, so EMUFS must resolve the
tension between "preserve the user's symlinks in what we show them" and
"resolve symlinks before deciding whether a path is inside the root".
Getting this wrong is a sandbox escape, so it deserves explicit design
and its own tests rather than being folded into feature work.

### 4. Address allocation

`&FC40-&FC43` and `&FC80-&FC87` are taken by existing extensions. A new
FRED block must also avoid collisions with real hardware; see the FRED
allocation table in `docs/hard-disc-comparison.md`. If a paged JIM window
is wanted, the page-select register lives in the extension's own FRED
bytes and the extension decodes offsets `0x100-0x1FF` itself -- the core
needs no change, but nothing in the core supports paging today either.

## Options

### Option A: emulate b2's BeebLink device

Implement the two-register device, use the unmodified BeebLink ROM, and
talk to the existing BeebLink server.

- **For:** cheapest by a wide margin. b2 proves the emulator side is
  ~220 lines. Immediately usable, with a mature server offering pluggable
  volume types (DFS, ADFS, native) and direct disc-image access.
- **Against:** requires the user to install a specific ROM build and run
  a separate Node server process. BeebLink has no protocol versioning --
  its own documentation says "there isn't any versioning mechanism, other
  than being a bit careful", which is why b2 pins a dated release and a
  specific `beeblink_b2.rom`. Server state lives outside the emulator,
  which is why b2 disables save states and rewind entirely when BeebLink
  is enabled (`BBCMicroCloneImpediment_BeebLink`, `BBCMicro.cpp:341`).

### Option B: EMUFS from scratch

Our own ROM, our own protocol, our own in-process host bridge.

- **For:** exact fit to Beebium's extension model, configuration, and
  client UI. No external process. Protocol versioning designed in from
  the start, consistent with the existing fingerprint handshake
  (`docs/versioning-and-compatibility.md`). State is in-process and
  therefore snapshottable.
- **Against:** the largest amount of new work, and the guest-side filing
  system is the hardest part of it. Reimplementing OSFILE/OSARGS/OSGBPB
  semantics correctly is a long tail of detail that BeebLink and VDFS
  have already paid for.

### Option C: BeebLink ROM and protocol, Beebium link driver and in-process host

BeebLink's link layer is an explicit abstraction: roughly ten required
symbols (`link_begin_send_*`, `link_send_payload_byte`,
`link_begin_recv`, `link_recv_payload_byte`, `link_startup`,
`link_unprepare`) plus optional weak-symbol fast paths for bulk memory
transfer. Four drivers exist (Tube Serial, UPURS, AVR, b2), and the b2
one was written specifically to be the simplest. Adding a Beebium driver
targeting our FRED registers is a self-contained job.

The host side would be ours, in-process, as a `1mhz-bus` extension.

- **For:** inherits a complete, proven, real-hardware filing system on
  the guest side, without inheriting the external server process. Keeps
  snapshot/rewind on the table. The link-driver seam is a documented
  extension point, so this is working with the design rather than against
  it.
- **Against:** couples us to BeebLink's protocol, including its known
  warts (see below) and its lack of versioning. Requires building the
  BeebLink ROM from source with an added driver, so the CMake ROM-assembly
  gap must be closed regardless. Divergence risk if upstream evolves.

### Recommendation

**Option C**, with **Option A first as a de-risking step.**

Standing up the b2-compatible device against the real BeebLink server
proves the entire bus path -- claim, dispatch, stretching, framing,
polling -- end to end for a small, well-understood amount of work, before
any ROM development is committed to. If that works, the incremental step
to Option C is a link driver plus an in-process host implementation, and
the device itself barely changes.

Option B remains the right answer if the coupling in Option C proves
uncomfortable in practice; nothing in Option C forecloses it, since the
device, the containment logic, and the ROM build rule are all reusable.

## Protocol lessons worth carrying over

These hold regardless of which option is chosen.

1. **Length-prefix every message.** BeebLink's 32-bit little-endian
   payload size means the emulated device frames packets without
   understanding a single message type -- b2's `WriteData` just counts
   bytes. New request types need zero emulator changes.
2. **Reserve type-space ranges up front.** BeebLink partitions its 7-bit
   type space so the framing layer can classify a message from one byte:
   ordinary requests, fire-and-forget (`$60-$6f`, no response expected),
   and speculative responses (`$70-$7f`, droppable). b2 relies on exactly
   this to detect fire-and-forget.
3. **Make errors in-band and native.** `RESPONSE_ERROR` carries a BBC
   error code and message; the ROM turns it into a `BRK`. Every failure,
   *including transport failure*, becomes an ordinary BBC error. b2
   synthesises an error packet when its HTTP bridge fails rather than
   hanging or raising a host dialog. The guest never has to distinguish
   "link broken" from "file not found".
4. **Design read-ahead and write-behind in from the start.** Byte-at-a-time
   I/O over a request/response link is one round trip per `OSBGET`.
   BeebLink retrofitted three separate mitigations (a short encoding for
   1-byte payloads, fire-and-forget `OSBPUT`, and speculative read-ahead
   responses) and its author still describes the result as poor. The
   speculative-response mechanism carries an arbitrary constraint -- it
   may only follow a response with a non-empty payload -- documented
   upstream with a `:(`.
5. **Do not use payload byte 0 as a message sub-type.** BeebLink did this
   early to conserve type space, stopped doing it later, and records that
   the space was never in danger and sub-types are more hassle to code.
6. **Version the protocol.** BeebLink's absence of versioning is its
   clearest self-identified mistake, and its later `OSWORD $9A` API
   reserves bytes for exactly that reason. Beebium already has a protocol
   fingerprint handshake; EMUFS should participate in it.

## Snapshot and rewind

Beebium has no snapshot or save-state facility today. `MachineState`
(`Machine.hpp:62-69`) carries a comment describing itself as
serialisable, but there are no serialise/deserialise functions.

This matters here because it is a fork in the road that is cheap to take
correctly now and expensive to revisit. b2 must disable save states and
rewind whenever BeebLink is active, because open file handles, the
current volume, drive and directory all live in a separate process.

If EMUFS's host side lives in-process as an extension, that state is
capturable in principle, and a future snapshot facility could include it.
An out-of-process design forecloses that permanently. This is the
strongest single argument for the in-process bridge in Options B and C,
and it is worth recording even though snapshots are not on the near-term
roadmap.

There is a related, smaller instance of the same issue already present:
the type-ahead queue's state lives in `TypeAheadQueue` outside
`MachineState`, whereas b2 keeps its paste state inside saved machine
state so that pastes survive rewind.

## Filing system semantics

Whichever option is chosen, the host-directory mapping must answer the
same questions. Both prior implementations converge closely, which is a
good sign that the answers are settled.

- **Metadata.** Acorn `.inf` sidecar files carrying load address, exec
  address, length, and lock attribute. Both VDFS (`vdfs.c:886-970`) and
  BeebLink use this; it is the de-facto standard and our disc tooling
  already understands the format. VDFS additionally merges real host
  attributes and modification times where available.
- **Filename escaping.** BBC names permit characters that are illegal or
  awkward on host filesystems, and vice versa. VDFS uses the beebwiki
  transliteration table (`#$%&.?@^` to `?<;+/#=>`, `vdfs.c:469-470`) with
  an `.inf` name field as an override. BeebLink uses a reversible `#XX`
  hex escape covering control characters, `/`, the Windows-illegal set,
  space, `.` (so a BBC file cannot collide with the `.inf` convention),
  and `#` itself -- plus escaping the leading character of Windows
  reserved names (`CON`, `PRN`, `AUX`, `NUL`, `COM0-9`, `LPT1-9`).
  **BeebLink's scheme is the better one**: reversible, and portable across
  all three platforms Beebium targets.
- **Drives and volumes.** DFS offers two surfaces; a host directory has
  no such constraint. BeebLink models a *volume* as a directory
  containing *drive* subdirectories named `0-9` and `A-Z`, giving 36
  drives, with cross-volume paths via a `::VOLUME:DRIVE:DIR.FILE` syntax.
  VDFS instead offers a mode switch: ADFS-like hierarchical (host
  subdirectories are BBC subdirectories) or DFS-like flat (the
  single-character directory becomes a filename prefix, and drive numbers
  are parsed then discarded).
- **The BBC directory character.** BeebLink folds it into the host
  filename (`$.!BOOT` is one file). This avoids a host directory per BBC
  directory letter and keeps `.inf` pairing simple.
- **Wildcards.** BeebLink permits them on read paths only, requires
  exactly one match, and returns "Ambiguous name" (204) otherwise; never
  permitted for save, delete, or rename. This is a sensible default.

## Relationship to other work

EMUFS is orthogonal to the Edit menu / clipboard work discussed
alongside it. A host filing system moves *files*; the clipboard moves
*text*. Neither subsumes the other:

- EMUFS does not help paste a few lines into a program being edited, a
  URL into a prompt, or an error message out into a bug report.
- The clipboard does not help move a 40 KB binary, a disc's worth of
  files, or anything being edited with host tools.

The two do interact in one place worth noting: several emulators
implement "copy BASIC listing" by typing `LIST` and capturing output,
with all the fragility that implies (hardcoded echo-prefix stripping,
heuristic termination detection). With EMUFS available, that feature is
better served by `SAVE`-ing to the host directory, and the clipboard
feature can be scoped to what a clipboard is actually good at.

## Open questions

1. **Option A first, or straight to C?** The de-risking argument is
   strong, but Option A requires the user to obtain a BeebLink ROM build
   and run its server, so it is awkward to ship even temporarily. It may
   be better as a throwaway validation branch than as a released feature.
2. **How much of BeebLink's server semantics do we adopt in Option C?**
   The volume/drive model, the escaping scheme, and the error-code
   mapping are all worth taking. The disc-image volume types probably
   duplicate our existing disc subsystem.
3. **Does the ROM get built from source, or checked in as a binary?**
   Building from source needs a CMake beebasm rule and makes beebasm a
   build dependency (currently it is an optional *test* dependency that
   tests skip cleanly without). Checking in a binary follows the existing
   `acorn-65c02-coprocessor` precedent but makes the ROM opaque.
4. **What is the containment policy exactly?** Confine to a single
   configured root; but decide the treatment of symlinks pointing outside
   it, of hard links, and of a root that is itself a symlink. Also decide
   whether write access is a separate opt-in from read access.
5. **Does the extension auto-install its sideways ROM, and in which
   slot?** This needs an answer in the extension API, not just for EMUFS.
   Interacts with `docs/sideways-slots.md` validation rules and with
   preset configuration.
6. **Should EMUFS appear as an `ExtensionStorage` device?** It would then
   show in client sidebars alongside discs and hard discs, with an
   activity indicator. `media_type` is free-form, so "host directory"
   fits, but the mount/eject affordances that clients offer for discs do
   not obviously apply.
7. **Is a paged JIM window warranted?** A simple command/status/data
   register block in FRED is sufficient for a request/response protocol.
   JIM paging would only pay off if bulk transfer through a 256-byte
   window proves materially faster than byte-at-a-time through a data
   port, which depends on the ROM's transfer loop rather than on the bus.
