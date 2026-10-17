# Machine Directories

Every machine Beebium runs has a directory of its own. A machine started
from a preset runs in a **transient** directory under the per-user
`machines/`, removed when it shuts down. A **saved** machine's directory
outlives the server and goes wherever the user puts it; every run continues
from the last. This document is the reference for what a machine directory
holds and in what formats. The design and its rulings are in
[discussion/presets-and-machine-directories.md](discussion/presets-and-machine-directories.md)
(section 4); the commands are in [cli.md](cli.md#saved-machine-subcommands)
and the RPCs in [grpc-server.md](grpc-server.md#savemachine).

## Layout

```
Elite Server.beebium-machine/
  machine.json                  identity: uuid, name, model, times, source preset
  preset.json                   the machine's configuration
  thumbnail.png                 its last screen
  nvram/<unit>.bin              the machine's battery-backed units
  nvram/stale/                  contents that could not be restored, kept
  extensions/<id>/nvram/<unit>.bin
                                an extension instance's battery-backed units
  lock                          held by the server running the machine
  Elite Server.beebium-machine  the document file
  desktop.ini, .directory       folder metadata (Windows, KDE)
  ...                           every file the preset held, at the same paths
```

The core defines the files above and nothing else. Everything else is the
machine's own media and files, laid out as the preset it was made from laid
them out; the core prescribes no `discs/` or `roms/`. A transient directory
holds only the copies of the media its preset named, and its lock.

A directory is a machine directory because it holds `machine.json`; neither
the `.beebium-machine` suffix nor the document file plays any part, so a
renamed directory still opens. The `.beebium-machine` suffix is the
convention for the directory and its document file alike; a directory named
without it still gets a document file with it.

## machine.json

```json
{
  "format": "beebium-machine",
  "version": 1,
  "uuid": "6f1c2a9e-...",
  "name": "L3 File Server, station {econet-station}",
  "model": "model-b",
  "created": "2026-10-17T15:28:56Z",
  "last_used": "2026-10-18T09:02:11Z",
  "source_preset": "model-b-l3fs-aun"
}
```

- `uuid` is the machine's identity, the `MachineIdentity` uuid the server
  reports on every run. A Save As gives the copy a new one.
- `name` is the name as the user gives it, a name template (see
  [cli.md](cli.md#machine-name)); a rename of a running saved machine
  (`SetMachineName`) is written here at once. It wins over a `machine_name`
  in `preset.json`.
- `model` is the model id; `start --machine` refuses a machine of another
  model.
- `created` and `last_used` are ISO 8601 UTC; `last_used` is empty until the
  first run, and written at every start.
- `source_preset` is the id of the preset the machine was made from, for
  information only; absent when it was made without one.

A `version` later than the server knows is refused. Clients never read this
file: `describe-machine` reports it.

## preset.json

The machine's configuration, in the preset format
([preset-schema.md](preset-schema.md)), loaded by `start --machine` exactly
as `--preset` loads a preset directory: references to files the directory
holds resolve within it, so the machine opens its own copies in place. It
records what was given when the machine was made -- the preset's settings and
any machine options given to `create-machine`, or to the launch of a machine
saved while running -- and every extension instance's id, pinned so the
instance finds its `extensions/<id>/` on every run. Options given to a
`start --machine` apply to that run only. The file is the user's to edit.

## Battery-backed state

What a real machine keeps while switched off -- an Integra-B's board RAM and
clock, an Acorn RTC's registers -- is kept in battery-backed units, one file
each:

- the machine's own in `nvram/<unit>.bin` (`integra-b-board-ram.bin`,
  `integra-b-rtc.bin`);
- an extension instance's in `extensions/<instance id>/nvram/<unit>.bin`
  (`extensions/clock/nvram/saf3019p.bin`).

A unit file is a header and the contents, integers little-endian:

| Bytes | Field |
|-------|-------|
| 8 | `BEEBNVRM` |
| 4 | the unit's layout version |
| 4, n | the length of the hardware descriptor, then the descriptor (text) |
| 8, n | the length of the contents, then the contents |

The files are written atomically at a graceful shutdown and on every Save, and
restored at power-on: the machine's after its ROMs are loaded, an extension's
after the extension is initialised. The preset's sideways loads into banks 4-7
are the template; restored nvram is applied after them. A file saved in
another layout version, or for other hardware (the descriptor differs), cut
short, or refused by its unit, is not restored: the unit starts from its seed,
one warning line says why, and the file is moved to `stale/` beside it,
renamed with the time, never over another. Nothing is discarded silently.

A unit with no file starts from its seed: the state of a board that has been
set up (an Integra-B as if IBOS's Full System Reset had been run and the
clock set), or, for an extension, what its configuration gives. That is every
start of a transient machine and the first start of a saved one.
`reset-battery`, or `SystemService.ResetBattery` on a running machine, removes
`nvram/` and every `extensions/*/nvram/`, so the next start is from the seed;
a running machine runs on unchanged and keeps nothing more that run. The
rest of `extensions/<id>/` is never touched, so a preset may ship files there
for an instance.

## thumbnail.png

The machine's screen, written on every Save and at a graceful shutdown of a
saved machine, through the capture path `capture-screenshot` uses (the
display-sized frame inside a 20-pixel black border). A machine made from a
preset has the preset's thumbnail until it has one of its own.

## lock

Held exclusively by the server running the machine (`flock` on POSIX,
`LockFileEx` on Windows), and released by the system if the process dies. The
holder's process id is written at the start of the file, so another process
can name it: a second `start --machine`, or `reset-battery`, on a running
machine exits with `TEMPFAIL` (75), "`<dir>` is in use by process `<pid>`", and
`describe-machine` reports the same id as `locked_by_pid`. The file is left
behind when the machine stops and is never read for anything else.

## The document file and folder metadata

The document file, `<name>.beebium-machine` inside the directory, is what a
user double-clicks on Windows and Linux (on macOS the directory itself is the
document). It only names the format and the machine:

```json
{
  "format": "beebium-machine",
  "version": 1,
  "uuid": "6f1c2a9e-..."
}
```

`start --machine` and `describe-machine` accept the document file in place of
the directory.

`desktop.ini` (UTF-16LE with a byte-order mark, `[.ShellClassInfo]` with
`LocalizedResourceName`) and `.directory` (`[Desktop Entry]` with `Icon` and
`Name`) give the folder the machine's display name and icon in Explorer and
KDE: its name, or the directory's when the name is a template only a running
server can render. On Windows `desktop.ini` is made hidden and system and the
directory read-only, which Explorer requires before it honours `desktop.ini`
(it does not make the contents read-only); the server clears that attribute
again before it renames or removes a machine directory. The server writes the
three files when a machine is made, on every Save and on a rename, and never
reads them: a machine directory with them deleted still opens.

## Saving a running machine

`SystemService.SaveMachine` (File > Save... in the macOS app) saves without
stopping the machine. With emulation paused, the server flushes the drives
and makes the destination whole beside it, as `<dest>.partial-<n>`: the
machine's own files, its `preset.json` (for a machine not yet saved, the
configuration recorded at launch, with the files its preset carries that it
has no copy of yet), `machine.json`, its battery-backed state and its screen.
It renames that into place, re-points every floppy and storage device open in
the old directory at its copy, moves the lock, and lets the old directory go:
a transient one is removed, a saved one is left, unlocked. The machine never
restarts.

| Machine | Destination | Result |
|---------|-------------|--------|
| not yet saved | a new directory | saved there, keeping its uuid |
| saved | none | its state, screen and name written in place |
| saved | a new directory | Save As: a copy with a new uuid, which the machine runs on from |

A destination must not exist, unless the request asks to replace a saved
machine there that no process runs and that is not this one; the old machine
is then put aside, replaced once the new one is whole, and removed, or put
back if anything fails. A save that fails leaves the machine where it was.

After a Save As, an Econet transport keeps the uuid it was given when the
machine started (the AUN transport's identity on the network) until the next
start. The directory the machine leaves holds its media as they were written
up to the moment of the Save, because media are written as the guest writes
them, while its battery-backed state and screen are as at its last Save.

While a machine not yet saved runs, the server reports on every status event
whether it has written to the copies of its preset's media
(`template_media_written`), which are lost unless it is saved; a front-end
can ask whether to save before closing.
