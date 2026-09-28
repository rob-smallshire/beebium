# Running an Acorn Level 3 File Server in Beebium

This document covers how to configure and run an Acorn Level 3 File Server
(L3FS) v1.26 within Beebium, primarily for testing virtual (AUN) Econet
networks. The companion document `FileServer-RTC-and-Timekeeping.md` covers
the RTC/dongle/timekeeping aspects in detail; this document focuses on disc
provisioning, command-line configuration, and the path towards automated
testing.


## Hardware Configuration

The L3FS requires a specific machine configuration:

- **Machine**: Model B with ROM/RAM board (`beebium-model-b-romram`)
- **Second processor**: 65C02 at 3MHz (the L3FS runs entirely on the parasite)
- **Floppy controller**: Acorn 1770 (for loading the FS code from SSD)
- **SCSI controller**: Acorn SCSI (the file server's data store)
- **Econet**: Enabled with a station number (conventionally 254 for a file
  server)
- **RTC**: Acorn User Port RTC with 7-bit-year-in-R7 layout (for v1.26)


## ROM Requirements

Three sideways ROMs are needed, each for a specific reason:

| Slot | ROM | Purpose |
|------|-----|---------|
| 9 | ANFS 4.18 | Contains the **Tube Host Code** for the 65C02 second processor. Without ANFS, the Tube will not initialise. Also provides NFS for Econet client access. |
| 10 | ADFS 1.30 | The L3FS stores its data on an ADFS-formatted SCSI disc. ADFS is needed to access the hard disc. |
| 11 | DFS 2.26 | The FS3v126.ssd boot disc is in SSD (DFS) format. DFS is needed to `*RUN` the file server code from floppy. Must be the 1770-compatible version. |

**Why ANFS and not DNFS?** DNFS 3.02/3.34 contain 8271-only DFS code which is
NOT compatible with the Acorn 1770 floppy controller. Since we need 1770 DFS
(for the SSD boot disc) AND Tube Host Code (for the 65C02), we use separate
DFS 2.26 and ANFS 4.18 ROMs. ANFS contains the same Tube Host Code as DNFS.


## Command Line

The L3FS can be reached over either Econet transport: AUN/UDP for hermetic
testing on a single host, or Piconet on a real Econet wire for use with
real BBC Microcomputers.

### Variant A: AUN/UDP (loopback testing)

A working command line for running the L3FS reachable over AUN:

```bash
./beebium-model-b-romram \
  --sideways 9:rom:roms/acorn-anfs_4_18.rom \
  --sideways 10:rom:roms/acorn-adfs_1_30.rom \
  --sideways 11:rom:roms/acorn-dfs_2_26.rom \
  --fdc acorn-1770 \
  --floppy 0:path/to/FS3v126.ssd \
  --acorn-scsi \
  --scsi-hdd 0:path/to/scsi-l3fs.dat \
  --station 254 \
  --aun port=10254:map=0.221@127.0.0.1@10221 \
  --machine-name "L3FS" \
  --acorn-rtc layout=7bit-year-in-r7 \
  --tube-65c02 \
  --advertise
```

The `map=` entries map Econet station addresses to IP:port pairs. The
inner separator is `;` (not `:`) because the extension argument parser
tokenises on `:`. For loopback testing, each station uses port `10000
+ station_number` by convention.

### Variant B: Piconet on a real Econet wire

To make the L3FS reachable from a real BBC Microcomputer (or any other
Acorn-compatible station) on a physical Econet network, swap the AUN flags
for `--piconet`:

```bash
./beebium-model-b-romram \
  --sideways 9:rom:roms/acorn-anfs_4_18.rom \
  --sideways 10:rom:roms/acorn-adfs_1_30.rom \
  --sideways 11:rom:roms/acorn-dfs_2_26.rom \
  --fdc acorn-1770 \
  --floppy 0:path/to/FS3v126.ssd \
  --acorn-scsi \
  --scsi-hdd 0:path/to/scsi-l3fs.dat \
  --station 250 \
  --piconet device_path=/dev/tty.usbmodem101 \
  --machine-name "L3FS-via-Piconet" \
  --acorn-rtc layout=7bit-year-in-r7 \
  --tube-65c02 \
  --advertise
```

Notes specific to the Piconet variant:

- **Pick a station number that's free on the wire.** The conventional FS
  station 254 may collide with another fileserver on the same wire (for
  example, a PiEconetBridge-hosted fileserver). Use `*STATIONS` from a real
  BBC to confirm the chosen number is free before launching.
- **`--piconet` and `--aun` are mutually exclusive.** Both are Econet
  transport extensions; BBC machine variants accept at most one. The
  Beebium server enforces this at startup.
- **Wire infrastructure must be in place:** clock generator on the wire,
  termination at both ends, and the Piconet attached via its standard Econet
  socket. See `docs/networking.md` and `docs/discussion/piconet-feasibility.md`
  for design background.
- **From the BBC, log in with:** `*I AM 0.<station> SYST` (using the L3FS
  station number from the command line). Followed by `*PASS "" <password>`
  on first contact, then the usual L3FS commands (`*CAT`, `*LCAT`, etc.).
- **Validated end-to-end** with a real BBC Microcomputer at station 221
  reaching a Beebium-emulated L3FS at station 250 via real Econet over a
  Piconet on `/dev/tty.usbmodem101`.

**Note:** `--scsi-hdd` requires the `.dat` image file, not the `.dsc`
geometry sidecar. Passing the `.dsc` by mistake results in a "Broken
directory" error from ADFS. The CLI should accept either `.dat` or `.dsc`
and locate the companion file automatically, since both must be present
side-by-side.


## SCSI Disc Image Provisioning

This is the main unsolved problem for automated testing. The L3FS needs a
SCSI hard disc image that has been:

1. **ADFS-formatted** (the underlying filesystem)
2. **Initialised with WFSINIT** (creates the L3FS partition within the ADFS
   free space)

### What WFSINIT Does

WFSINIT is a BBC BASIC program that partitions an ADFS-formatted disc into
two regions:

- An **ADFS section** at the beginning (typically 16KB, enough for the `$`
  root directory and system files)
- A **Level 3 (AFS0) partition** occupying the remaining free space

The partition process:

1. Reads the ADFS free space map (sectors 0-1)
2. Finds the first free space entry and assumes it is the only one (the disc
   must be compacted first)
3. Reduces the ADFS free space to leave ~16KB for ADFS
4. Writes the AFS0 Disc Information Block at the start of the L3FS partition
5. Initialises per-track sector bitmaps
6. Creates the root directory (`$`)
7. Optionally creates a passwords file with default user `SYST`
8. Updates the ADFS free space map to hide the L3FS partition

### AFS0 Disc Structure

The L3FS uses the AFS0 filesystem format (documented at
`http://mdfs.net/Docs/Comp/Disk/Format/AFS0`):

- **256-byte logical sectors** with 24-bit addressing (up to 4GB)
- **Per-track sector bitmaps**: sector 0 of each track contains a bitmap;
  set bits = free, clear bits = allocated
- **Disc Information Block** (NFS sector 1, i.e. offset 0x100 into the
  partition):
  - Bytes 0x00-0x03: `AFS0` identifier
  - Bytes 0x04-0x13: disc title (10 chars, space-padded)
  - Bytes 0x14-0x15: number of tracks
  - Bytes 0x16-0x18: total disc sectors (3 bytes)
  - Byte 0x19: partitions (usually 1)
  - Bytes 0x1A-0x1B: sectors per track
  - Byte 0x1C: sectors per bitmap
  - Bytes 0x1F-0x21: root directory SIN (Sector Identification Number)
  - Bytes 0x22-0x23: initialisation date
- **Allocation maps** use `JesMap` signature (6 bytes) followed by sector
  group entries (3-byte address + 2-byte count, 5 bytes each)
- **Directories**: up to 26 sectors (0x1A00 bytes), linked-list entries of
  26 bytes each

### Available Disc Images

#### Blank ADFS images (`scripts/adfs-disc-tools/`)

The `adfs-disc-tools` package (`scripts/adfs-disc-tools/`) bundles 22 blank,
pre-formatted ADFS hard disc images (2 MB to 512 MB) from
[Jon Ripley's BBC Micro Hard Drives page](https://jonripley.com/8bit/HardDrives/).

CLI usage (from `scripts/adfs-disc-tools/`):

```bash
uv run adfs-disc-tools list-sizes
uv run adfs-disc-tools create-image 16 /tmp/l3fs-data.dat
```

Library usage:

```python
from adfs_disc_tools import extract_blank_adfs_image
extract_blank_adfs_image(Path("/tmp/l3fs-data.dat"), size_mb=16)
```

Available sizes: 2, 4, 8, 16, 20, 24, 32, 40, 48, 56, 64, 80, 96, 100,
128, 192, 200, 256, 300, 320, 400, 512 MB.

These blank images are the starting point for L3FS provisioning — they are
ADFS-formatted but need WFSINIT to create the AFS0 partition.

#### Beebium C++ test assets (`tests/assets/scsi/`)

| File | Description |
|------|-------------|
| `scsi0.dat` | BeebEm ADFS-formatted SCSI image (~10 MB) with sample files. Used by existing C++ integration tests. |
| `scsi0.dsc` | BeebEm SCSI geometry descriptor for scsi0.dat. |

#### BeebEm images (`/Users/rjs/Code/beebem-windows/UserData/DiscIms/`)

| File | Size | Description |
|------|------|-------------|
| `L3FS-ISW.adl` | 640KB | ADFS floppy — the L3FS Initial Software disc. **Contains WFSINIT.** |
| `l3server.adl` | 640KB | ADFS floppy containing the L3FS *code* (not a data disc). Contains AFS0 references in the file server source, not as filesystem structure. |
| `L3-Utils.dsd` | 400KB | DFS double-sided disc with L3 utilities (Library, Library1, Utils). |

**None of these are a ready-to-use L3FS data disc.** A blank ADFS image from
`BBCHDDs.zip` would need to be WFSINIT'd (using `L3FS-ISW.adl`), or a new
image constructed programmatically.

### Boot Disc

The L3FS v1.26 code is loaded from:

| File | Location | Format |
|------|----------|--------|
| `FS3v126.ssd` | `/Users/rjs/Code/L3V126/FS3v126.ssd` | DFS SSD |

The file server is started with `*RUN FS3v126` after booting from this disc.
The code relocates itself to the 65C02 parasite processor.


### Why v1.26

`FS3v126` is not an Acorn release. It is a third-party build by mmbeeb
(`https://github.com/mmbeeb/L3V126`), produced from the v1.06 source updated
to v1.24 with further changes, and it runs as a disc executable rather than a
sideways ROM. It is used here because it is the only Level 3 file server
version whose User Port real-time-clock dongle path handles 21st-century
dates, which is exactly what the `acorn-rtc` extension (SAF3019P emulation)
needs to exercise. Per the mmbeeb README, the time/date functions "now handle
21st century dates", and the single binary tests for the dongle first, falls
back to OSWORD 14 (an onboard RTC), then to manual entry.

None of the alternatives serve this purpose:

- Acorn originals v0.90 to v1.24 store a 4-bit year offset from 1981, so the
  dongle covers 1981-1996 only.
- J.G. Harston's Y2K patches sidestep the dongle: v0.92 is assembled with no
  dongle code at all, and v1.25 never writes the dongle, reading the Master's
  onboard RTC via OSWORD 14 instead.
- v1.31/v1.33 write a 7-bit year into alarm register 1, which has only 5 data
  bits on the SAF3019P, so years from 2001 wrap back into 1981-2000 on read.

v1.26 instead stores the absolute two-digit year in alarm register 7 (the
7-bit minute alarm) and moves dongle detection to register 5, giving
1981-2099 with a working dongle round trip. The `acorn-rtc` extension
implements this as the `7bit-year-in-r7` register layout, and
`tests/test_saf3019p_v126.cpp` replicates v1.26's exact CBUS byte sequences
from `Uade04.asm`. The register-7 year convention is unique to v1.26: a
dongle written by any other version is not readable by it, and vice versa.
See `docs/FileServer-RTC-and-Timekeeping.md` and `docs/acorn-user-port-rtc.md`.

A note on naming. I very much dislike the all-too-common community practice
of extending, or worse, inserting into, the original vendor's version numbers
with unofficial modifications. I much prefer that people use something like a
personal three-letter suffix, L3FSv106MMX rather than inventing L3FSv126.
Invented version numbers make the real history incredibly difficult to
follow, especially if you were not there at the time and lack the tribal
knowledge of Acorn culture.

## Where to Get WFSINIT

Several sources exist:

1. **BeebMaster's L3Utils3**: `https://www.beebmaster.co.uk/Downloads/L3Utils3.zip`
   - Contains WFSINIT v1.7 (2020), compatible with ADFS 1.33 and 1.53
   - Available as a single ADFS disc image (`L3Utils3.adf`)
   - Also contains Library, Library1, and Utils directories needed for
     client stations

2. **JGH's mdfs.net**: `http://mdfs.net/Apps/Networking/FServers/`
   - Updated WFSINIT versions tweaked for non-SCSI drives (IDE, GoMMC)
   - File server documentation and AFS0 format specification

3. **Stardot forum threads**:
   - `https://stardot.org.uk/forums/viewtopic.php?t=27782` — Setting up L3FS
     in BeebEm, step-by-step walkthrough
   - `https://stardot.org.uk/forums/viewtopic.php?t=28164` — WFSINIT with IDE
     discs, memory limits, geometry parameters

### WFSINIT Limitations

- Assumes the disc has been **compacted** (all free space contiguous)
- On a BBC Model B, memory limits initialisation to ~260MB (64 bytes per MB
  for the bitmap)
- Original version fails with large IDE/CF discs; JGH's tweaked version
  handles larger capacities
- For SCSI, disc geometry is auto-detected; for IDE, it must be entered
  manually


## File Server Startup Sequence

Once the SCSI disc is initialised:

1. Boot from DFS: `*DISC` (select DFS filing system)
2. Load the file server: `*RUN FS3v126`
3. The code transfers to the 65C02 parasite
4. On the parasite, the FS:
   - Detects the RTC dongle (or prompts for date if `DONGLE=1`)
   - Reads the SCSI disc bitmap (can take minutes for large discs)
   - Prompts for number of drives
   - Prompts for station count (1-40)
   - Displays "Starting - Ready"

### First-Time Configuration (from another station)

After the file server is running, from a client station:

```
*I AM SYST
*PASS "" <password>
*CDIR <username>
*SETFREE <username> <bytes>
```


## SUPERFORM: SCSI Disc Formatting

Before WFSINIT can initialise the L3FS partition, the SCSI disc must be
ADFS-formatted. The BeebEm scsi0.dat image contains Hugo's SUPERFORM utility,
which handles low-level formatting of SCSI drives. For emulated SCSI, ADFS
formatting with `*FORM` or SUPERFORM creates the free space map that WFSINIT
then partitions.


## Path to Automated Testing

The goal is to provision a L3FS entirely programmatically for use in automated
Econet integration tests, without manual interaction with WFSINIT.

### Approach 1: Binary Disc Image Construction

Write a tool (Python or C++) that constructs a valid L3FS SCSI disc image
directly, without running WFSINIT inside the emulator:

1. Create an ADFS free space map (sectors 0-1)
2. Write the AFS0 Disc Information Block at the partition boundary
3. Initialise per-track sector bitmaps
4. Create the root directory with `SYST` user
5. Create the passwords file
6. Write the allocation maps with `JesMap` headers

The AFS0 format is fully documented at `http://mdfs.net/Docs/Comp/Disk/Format/AFS0`.
This approach gives complete control over the disc layout and is fully
deterministic and reproducible. It avoids the complexity of driving the
emulator through WFSINIT's interactive BASIC program.

### Approach 2: Scripted Emulator Interaction

Use the gRPC debugger and keyboard services to:

1. Launch Beebium with ADFS and a blank SCSI image
2. Script the ADFS formatting commands via keyboard input
3. Load and run WFSINIT, feeding it responses via keyboard input
4. Capture the resulting disc image

This is more fragile (depends on screen scraping or timing) but validates
the real WFSINIT code path.

### Approach 3: Pre-built Golden Image

Create a WFSINIT'd disc image once (manually or via Approach 2), commit it
to the test assets, and use it as a fixture. This is the simplest approach
but the disc image is opaque and hard to modify.

### Recommended Approach

**Approach 1 (binary construction)** is the most robust for CI. The AFS0
format is straightforward — the critical structures are:

- ADFS free space map (2 sectors, well-documented)
- AFS0 Disc Information Block (1 sector, 37 significant bytes)
- Per-track bitmaps (1 sector per track, simple bitfield)
- Root directory (1 sector minimum, 11-byte header + linked entries)
- Allocation map (1 sector, `JesMap` header + 5-byte groups)

A Python script of ~200-300 lines could construct a valid minimal L3FS image.
This could live in the integration test fixtures alongside the existing
`conftest.py` for L3FS clock tests.


## Existing Test Infrastructure

### L3FS Clock Tests (`integration_tests/l3fs-clock/`)

These tests boot the L3FS to verify RTC polling timing. They use:

- `conftest.py`: pytest fixtures, configurable via `L3FS_SSD` environment
  variable (default: `/Users/rjs/Code/L3V126/FS3v126.ssd`)
- `test_l3fs_clock_update.py`: monitors RTC RDDONG calls via gRPC
  WatchActivity stream

These tests exercise the RTC path but do not test Econet networking — the
file server starts but no clients connect.

### Unit Tests (`tests/test_saf3019p_v126.cpp`)

329-line test suite validating SAF3019P emulation against exact byte sequences
from the L3v126 `Uade04.asm` source. Tests dongle detection, RDDONG/WRDONG
operations, and the V126 7-bit year layout.

### Econet File Server Tests (`tests/test_econet_fileserver.cpp`)

498-line integration test suite for Econet communication with an external
file server (via BeebEm or real hardware). Configurable via
`BEEBIUM_FILESERVER` environment variable. These currently require a
manually-started external file server.

### Econet Boot Tests (`tests/test_boot_econet.cpp`)

Boot tests with Econet fitted, verifying NFS ROM loading.


## Future Work: Automated Econet Network Tests

The end goal is a test harness that:

1. Constructs a fresh L3FS SCSI disc image (Approach 1)
2. Launches a Beebium file server instance with the configuration above
3. Launches one or more Beebium client instances with `--station N
   --aun port=10N:map=0.254@127.0.0.1@10254`
4. Waits for the file server to reach "Starting - Ready"
5. On client stations, exercises Econet operations: `*I AM`, `*CAT`, file
   load/save, `*NOTIFY`, `*REMOTE`
6. Verifies results via gRPC (screen content, disc events, Econet events)
7. Tears down all instances

This would replace the current dependency on an external BeebEm file server
and enable fully self-contained Econet testing in CI.


## References

- `docs/FileServer-RTC-and-Timekeeping.md` — RTC dongle and timekeeping
- `docs/acorn-user-port-rtc.md` — SAF3019P hardware details
- `docs/econet-integration.md` — Econet/AUN gRPC integration plan
- `docs/networking.md` — AUN networking implementation
- `docs/local-beebem-econet-lessons.md` — Lessons from BeebEm's Econet work
- AFS0 format: `http://mdfs.net/Docs/Comp/Disk/Format/AFS0`
- BeebMaster L3 setup: `https://www.beebmaster.co.uk/Econet/Level3.html`
- BeebMaster disc structure PDF: `https://www.beebmaster.co.uk/Downloads/Understanding%20the%20Acorn%20Level%203%20File%20Server%20Structure.pdf`
- Stardot L3FS in emulators: `https://stardot.org.uk/forums/viewtopic.php?t=27782`
- Stardot WFSINIT with IDE: `https://stardot.org.uk/forums/viewtopic.php?t=28164`
- L3FS v1.26 source: `/Users/rjs/Code/L3V126/` (disassembly)
