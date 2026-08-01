# Gating front-end features on a local server

Some front-end features only work when the server is running on the same host
as the client. This describes how a front end finds that out and what it must
do about it. Platform-independent rules first, then the macOS specifics, for
future front ends.

## The problem

Several parts of the protocol exchange **filesystem paths**:

- `InsertDisc` takes a URL that the *server* opens. A path chosen with the
  client's file picker, or dragged in from its file manager, names a file on
  the client's host.
- `DriveStatus.disc_url` and a storage device's backing path are paths on the
  *server's* host. A front end that offers "reveal this in the file manager"
  is opening them on the client's host.

Both are meaningful only when the two processes share a filesystem. With a
server on another machine, inserting a disc fails with a confusing "no such
file" from somewhere the user never looked, and revealing one silently opens
nothing, or -- worse -- opens a different file that happens to sit at the same
path locally.

This is not an argument for browsing remote filesystems. It is an argument for
not offering a control that cannot work.

## Finding out

`SystemInfo.host_fingerprint` (system.proto) is an opaque token identifying the
host the server runs on. A client derives its own the same way and compares
them; equal means the two processes share a filesystem.

Network addresses cannot answer this. A client may reach a server on its own
machine over loopback, over that machine's LAN address, or through a Bonjour
`.local` name that resolves back to itself -- three different-looking routes to
the same host. The Bonjour case is not exotic: it is what discovery produces
for a server on the very machine doing the discovering.

### Deriving the fingerprint

`src/service/include/beebium/service/HostFingerprint.hpp`, over
`beebium::platform::host_identifier()`:

| Platform | Host identifier |
|----------|-----------------|
| macOS    | `gethostuuid()` |
| Linux    | `/etc/machine-id`, falling back to `/var/lib/dbus/machine-id` |
| Windows  | `HKLM\SOFTWARE\Microsoft\Cryptography\MachineGuid` |

The fingerprint is `SHA-256("beebium-host-v1:" + identifier)`, lowercase hex.

Two properties are deliberate:

- **It is hashed.** The underlying value is a hardware or OS-installation
  identifier that outlives Beebium and identifies the machine to anything else
  that asks for it. A server answers `GetSystemInfo` to any client that
  connects, so that identifier should not be how it travels a network. A digest
  compares exactly as well.
- **A container reads as a different host.** It has its own `machine-id` and
  its own filesystem, so a path from outside it does not mean what the sender
  intended. Reporting "not the same host" is the correct answer, not a
  limitation.

The domain prefix is part of the protocol. Changing it on one side only makes
every peer look as though it is somewhere else.

## Rules for a front end

1. **Expose the question, not the mechanism.** Fingerprints and hashes belong
   wherever the comparison happens. The rest of the front end should see one
   boolean -- `isServerLocal` in the macOS client -- because that is the
   question every caller is actually asking.
2. **Fail closed.** An empty fingerprint, or a server not yet asked, means *not
   local*. Treating unknown as local enables precisely the features that break,
   in the case where least is known. Two empty values are not a match.
3. **Disable, don't fail.** A control that cannot work should be unavailable
   and say why, rather than being offered and then erroring.
4. **Re-evaluate on reconnect.** The verdict belongs to a connection, not to
   the app; reset it on disconnect.

### What to gate

| Feature | Local only? | Why |
|---------|-------------|-----|
| Browse for a disc image | Yes | Picks a path on this host for the server to open |
| Drag and drop a disc image | Yes | Same, from the file manager |
| Reveal in file manager | Yes | Opens a path the server reported |
| Reveal a sideways ROM image | Yes | Opens a path the server reported |
| Copy path | **No** | Still the path the server reported, and still worth quoting -- to paste into a terminal on that host, or into a bug report |

Copy Path is the useful boundary case: it hands the user a string rather than
acting on it, so it stays honest whatever host the server is on.

### An existence check is not a gate

The tempting shortcut is to test whether the path exists before offering to
reveal it. That question is asked of the *client's* filesystem about a path
from the *server's*, so it is wrong in both directions: it hides the command
for a local server whose file has since moved, and -- worse -- it succeeds
against a remote server whose host has a similar install layout, revealing a
different file that merely shares a path. Silence would have been better.

Check `isServerLocal` first. An existence check is still worth keeping after
it, to catch a file deleted since the server loaded it.

## macOS specifics

- `HostFingerprint.swift` derives the client's own token via `gethostuuid()` and
  CryptoKit, mirroring the C++ recipe. The app is not sandboxed, so
  `gethostuuid()` answers; a sandboxed build would need re-checking.
- `SystemClient.isServerLocal` is the single published boolean, set from
  `GetSystemInfo` and cleared on `disconnect()`.
- `StorageModeView` gates the browse button (with an explanatory tooltip), the
  drop delegate (which refuses with "Server is on another host" while the drag
  hovers, so the refusal is visible before the pointer is released), the
  empty-drive prompt, and Reveal in Finder on both floppy rows and
  peripheral-published storage rows such as hard discs.
- `MemoryModeView` gates Reveal in Finder on a sideways slot's image. The
  Memory sidebar is read-only -- the client never calls `ConfigureSlot` -- so
  the slot's `image_name` only ever travels server-to-client.
- Views take `isServerLocal` as a plain parameter rather than a `SystemClient`
  to consult, so what they depend on is visible in their signature.

## What does not need gating, and why

Not every path in a front end crosses the boundary. Three cases recur:

- **Pre-launch configuration.** The macOS `ConfigurationEditor` picks disc and
  sideways ROM images before any server exists; the machine it configures is
  spawned afterwards as a local process, and the paths become its command-line
  arguments. They always mean here. This holds only while launching is local:
  a flow that ever started a machine on another host would break it, so the
  reasoning is recorded in the editor itself rather than left to be
  rediscovered.
- **The client's own files.** Keyboard mappings and preset import/export read
  and write files belonging to the client. The server never sees them.
- **Read-only server paths that are only displayed.** Showing a path, in a
  row or a tooltip, is as honest as copying one.

The audit behind this list covered every `NSOpenPanel`, file drop, reveal and
path-carrying request in the macOS client. A front end for another platform
should do the same sweep rather than assume this list transfers.
