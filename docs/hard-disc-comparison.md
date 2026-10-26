# Hard Disc Emulation Comparison

A survey of hard disc support across BBC Micro emulators (B-Em, b2, jsbeeb, beebjit, BeebEm) and hardware emulation projects (BeebSCSI, Pi1MHz), compared to Beebium.

## Support Overview

| Emulator | Hard Disc Support | Controllers | Image Formats |
|----------|------------------|-------------|---------------|
| B-Em | Yes | SCSI, IDE | DAT+DSC, HDF |
| b2 | Yes (recent) | SCSI | DAT+DSC |
| jsbeeb | No | - | - |
| beebjit | No | - | - |
| BeebEm | Yes | SCSI, IDE, SASI | DAT+DSC |
| Beebium | No | - | - |
| **Hardware** | | | |
| BeebSCSI | Yes (hardware) | SCSI | DAT+DSC, DAT+CFG |
| Pi1MHz | Yes (hardware) | SCSI | DAT+DSC, DAT+CFG |

B-Em, b2, and BeebEm implement hard disc emulation in software. b2's SCSI support is a recent addition (June 2025 onwards, stabilised March 2026). jsbeeb and beebjit are floppy-only (beebjit's README explicitly lists hard drives as out of scope).

Two hardware projects -- BeebSCSI and Pi1MHz -- provide high-quality SCSI emulation that plugs into a real BBC Micro's 1 MHz bus. Their implementations are more complete than the software emulators and serve as authoritative references for the Acorn SCSI host adapter protocol.

## Hard Disc Controllers

### Real Hardware Background

The BBC Micro's hard disc ecosystem was built around the 1 MHz expansion bus. The two main approaches were:

- **SCSI via Acorn ADFS board**: An Acorn-designed host adapter with a WD2793-based data separator, connected to a SASI/SCSI bridge (typically Adaptec ACB-4000) and then a standard SCSI hard drive. The host adapter presents a simplified SASI-like register interface at 0xFC40-0xFC43 on the FRED page.
- **IDE (later)**: Direct IDE register interface at 0xFC40-0xFC47, used by various third-party boards.

Both share the same base address (0xFC40) on the FRED page and are mutually exclusive -- a machine would have one or the other, never both.

A third variant existed: the **Torch SASI controller** at `0xFDF0-0xFDF3` on the JIM page, used with Torch Z80 Communicator systems. Despite being associated with the Z80 coprocessor, this controller sits on the **host BBC Micro's 1 MHz bus** -- the Z80 parasite has no independent storage hardware and accesses discs indirectly through a non-standard Tube-like communication link to the host. The Torch SASI controller talked to a Xebec S1410 bridge board connected to a physical Winchester drive (10 or 20 MB).

### Controller Comparison

| Feature | B-Em SCSI | B-Em IDE | b2 SCSI | BeebEm SCSI | BeebEm IDE | BeebEm SASI |
|---------|-----------|----------|---------|-------------|------------|-------------|
| Max drives | 4 (LUN 0-3) | 2 | 4 (LUN 0-3) | 4 (LUN 0-3) | 4 | 1 |
| Sector size | 256 bytes | 256 bytes (512 padded) | 256 bytes | 256 bytes | 256 bytes | 256 bytes |
| I/O base | 0xFC40 | 0xFC40 | 0xFC40 | 0xFC40 | 0xFC40 | 0xFC40 |
| Registers | 4 (+00 to +03) | 9 (+00 to +08) | 4 (+00 to +03) | 4 (+00 to +03) | 8 (+00 to +07) | 4 (+00 to +03) |
| Protocol | Phase-based bus | ATA registers | Phase-based bus | Phase-based bus | ATA registers | Phase-based bus |
| I/O page | FRED | FRED | FRED | FRED | FRED | JIM |
| Machine target | Model B, Master | Model B, Master | Model B, Master, Electron | Model B, Master | Model B, Master | Torch Z80 host |

## SCSI Command Sets

The software emulators implement minimal command sets sufficient for ADFS, while the hardware projects (BeebSCSI, Pi1MHz) implement substantially more complete SCSI-1 compliance.

| Command | Code | B-Em | b2 | BeebEm | BeebSCSI | Pi1MHz | Purpose |
|---------|------|------|----|--------|----------|--------|---------|
| TEST UNIT READY | 0x00 | Yes | Yes | Yes | Yes | Yes | Check drive presence |
| REZERO UNIT | 0x01 | - | - | Yes (SASI) | Yes | Yes | Seek to track 0 |
| REQUEST SENSE | 0x03 | Yes | Yes | Yes | Yes | Yes | Error status |
| FORMAT UNIT | 0x04 | Yes | Yes | Yes | Yes | Yes | Initialise disc |
| REASSIGN BLOCKS | 0x07 | - | - | - | - | Yes | Defect mapping |
| READ (6) | 0x08 | Yes | Yes | Yes | Yes | Yes | Read sectors (21-bit LBA) |
| VERIFY | 0x09 | - | - | Yes (SASI) | - | - | Verify sectors |
| WRITE (6) | 0x0A | Yes | Yes | Yes | Yes | Yes | Write sectors (21-bit LBA) |
| SEEK | 0x0B | - | - | Yes (SASI) | Yes | Yes | Seek to block |
| SET GEOMETRY | 0x0C | - | - | Yes (SASI) | - | - | Configure geometry |
| TRANSLATE | 0x0F | Yes | Yes | Yes | Yes | Yes | LBA to CHS translation |
| INQUIRY | 0x12 | - | - | - | - | Yes | Device identification |
| MODE SELECT (6) | 0x15 | Yes | Yes | - | Yes | Yes | Set device parameters |
| MODE SENSE (6) | 0x1A | Yes | Yes | - | Yes | Yes | Read device parameters |
| START/STOP UNIT | 0x1B | Yes | Yes | - | Yes | Yes | Spindle control |
| SEND DIAGNOSTIC | 0x1D | - | - | - | - | Yes | Self-test |
| READ CAPACITY | 0x25 | - | - | - | - | Yes | Query disc size |
| WRITE (10) | 0x2A | - | Yes | - | - | - | Extended write (32-bit LBA) |
| WRITE AND VERIFY | 0x2E | - | Yes | - | - | - | Write with verification |
| VERIFY (10) | 0x2F | Yes | Yes | Yes | Yes | Yes | Range check |
| READ DEFECT DATA | 0x37 | - | - | - | - | Yes | Defect list |
| RAM DIAGNOSTICS | 0xE0 | - | - | Yes (SASI) | - | - | Self-test |
| CONTROLLER DIAG | 0xE4 | - | - | Yes (SASI) | - | - | Self-test |
| *Vendor-specific* | 0x10-0x14 | - | - | - | Yes | Yes | BeebSCSI extensions |

The minimum viable set for ADFS operation is: TEST UNIT READY (0x00), REQUEST SENSE (0x03), READ (0x08), and WRITE (0x0A). However, MODE SELECT (0x15) and MODE SENSE (0x1A) are needed for geometry configuration, and FORMAT (0x04) is needed for disc initialisation.

## IDE Command Sets

| Command | Code | B-Em | BeebEm | Purpose |
|---------|------|------|--------|---------|
| RESTORE | 0x10 | Yes | - | Seek to cylinder 0 |
| READ SECTOR(S) | 0x20 | Yes | Yes | Read sector data |
| WRITE SECTOR(S) | 0x30 | Yes | Yes | Write sector data |
| READ VERIFY | 0x40 | Yes | - | Verify readable |
| FORMAT TRACK | 0x50 | Yes | - | Format track |
| SET PARAMETERS | 0x91 | Yes | Yes | Configure geometry |
| SEEK | 0x70 | Yes | - | Seek to cylinder |
| IDLE | 0xE3 | Yes | - | Power management |
| IDENTIFY DEVICE | 0xEC | Yes | - | Drive identification |

## SCSI Bus Protocol

Both B-Em and BeebEm implement a phase-based SCSI/SASI bus protocol. The bus transitions through phases in sequence:

```
BUS FREE → SELECTION → COMMAND → EXECUTE → DATA (Read/Write) → STATUS → MESSAGE → BUS FREE
```

### Bus Signals

| Signal | Direction | Purpose |
|--------|-----------|---------|
| BSY | Target→Host | Target is active |
| SEL | Host→Target | Target selection |
| CD | Target→Host | Command (1) vs Data (0) |
| IO | Target→Host | Input to host (1) vs Output from host (0) |
| MSG | Target→Host | Message phase active |
| REQ | Target→Host | Target ready for transfer |
| IRQ | Target→Host | Interrupt request |

### Register Interface (0xFC40-0xFC43)

| Offset | Read | Write |
|--------|------|-------|
| +00 | Data register | Data register |
| +01 | Status (CD, IO, MSG, BSY, REQ, IRQ) | - |
| +02 | - | Select register |
| +03 | - | IRQ enable |

The status register encodes bus phase via CD, IO, and MSG bits:

| CD | IO | MSG | Phase |
|----|----|----|-------|
| 1 | 0 | 0 | Command |
| 0 | 1 | 0 | Data In (read) |
| 0 | 0 | 0 | Data Out (write) |
| 1 | 1 | 0 | Status |
| 1 | 1 | 1 | Message |

## Hard Disc Image Formats

### DAT + DSC (SCSI/SASI)

Used by both B-Em and BeebEm for SCSI and SASI drives.

**DAT file**: Raw sector data. Each sector is 256 bytes, stored sequentially at offset `LBA * 256`. No header, no metadata, no container structure.

**DSC file**: 22-byte binary geometry descriptor.
- Bytes 13-14: Cylinder count (little-endian)
- Byte 15: Head count
- Sectors per track: fixed at 33
- Total capacity: `cylinders * heads * 33 * 256` bytes

Example (BeebEm scsi0.dsc): 306 cylinders, 4 heads = 40,392 sectors = ~9.9 MB

If no DSC file is present, geometry is auto-calculated from the DAT file size.

**Format detection**: B-Em checks for a "Hugo" signature at offset 0x200 (standard ADFS) or a null-padded "Hugo" at offset 0x400 (IDE-padded format) to distinguish between SCSI and IDE sector layouts.

### HDF (IDE)

Used by B-Em only for IDE drives.

**HDF file**: Raw IDE disc image with 256-byte sectors. Sector addressing: `(cylinder * heads_per_cylinder + head) * sectors_per_track + sector) * 256`.

Default geometry: 101 cylinders, 16 heads, 63 sectors/track. Configurable via the ATA SET PARAMETERS (0x91) command.

### Pre-formatted Images

| Emulator | Type | Drives | Sizes |
|----------|------|--------|-------|
| B-Em | SCSI (DAT) | scsi0-scsi3 | ~10 MB each |
| B-Em | IDE (HDF) | hd4, hd5 | Variable |
| BeebEm | SCSI (DAT) | scsi0-scsi3 | ~10 MB (scsi0), stubs (scsi1-3) |
| BeebEm | IDE (DAT) | ide0-ide3 | ~4 MB each |
| BeebEm | SASI (DAT) | sasi0 | 16 MB |

### Image Creation

B-Em includes an `hdfmt` utility that creates ADFS-formatted hard disc images:
- Creates paired DAT + DSC files
- Initialises ADFS root directory ("Hugo" signature)
- Initialises free space map (sectors 0-4)
- Auto-adjusts size to fit geometry (33 sectors/track, 4 heads)
- Usage: `hdfmt <basename> <size>` (supports k/m/g suffixes)

BeebEm provides pre-formatted images but no creation tool.

## Filing System Requirements

Hard disc access on the BBC Micro requires ADFS (Advanced Disc Filing System). The ADFS version numbering encodes both the target machine and the hard drive interface type ([JGH's definitive list](https://stardot.org.uk/forums/viewtopic.php?p=81472#p81472)):

- `ADFS x.x0` = SCSI hard drive
- `ADFS x.x3` = IDE hard drive
- `ADFS 1.3x` = BBC B/B+, Tube and hard drive support
- `ADFS 1.5x` = Master/Compact, Tube and hard drive support
- `ADFS 2.xx` = Master/Compact, includes `*FORMAT`/`*VERIFY` in ROM

| ROM | Controller | Machine | Notes |
|-----|------------|---------|-------|
| ADFS 1.00 | SCSI | Electron | |
| ADFS 1.30 | SCSI | BBC B/B+ | Standard SCSI version for B and B+ |
| ADFS 1.33 | IDE | BBC B/B+ | |
| ADFS 1.50 | SCSI | Master/Compact | Included in MOS 3.20 |
| ADFS 1.53 | IDE | Master/Compact | |
| ADFS 2.03 | SCSI | Master/Compact | Included in MOS 3.50 |
| ADFS 2.10 | SCSI | Master/Compact | Included in MOS 5.xx |
| HADFS 5.30+ | IDE | Various | |

See also: http://mdfs.net/System/ROMs/Filing/Disk/Acorn/

Hard disc drives appear as ADFS drives 0-3, with floppy drives at 4-5.

## 1 MHz Bus Architecture

The hard disc controllers sit on the BBC Micro's 1 MHz expansion bus, which provides access to two I/O pages:

| Page | Address | Name | Purpose |
|------|---------|------|---------|
| FRED | 0xFC00-0xFCFF | Fast Read/Expand Data | Peripheral control registers |
| JIM | 0xFD00-0xFDFF | Joint I/O Memory | Paged data memory window |

### Bus Timing

All accesses to FRED and JIM are stretched to 1 MHz timing (1 microsecond per access), regardless of the CPU's native clock speed. This is important for cycle-accurate emulation -- the CPU stalls during 1 MHz bus accesses.

### Address Allocation

The SCSI/IDE controller uses only FRED page addresses:

| Address | Peripheral |
|---------|------------|
| 0xFC00-0xFC03 | ExtMem paging (b2 Opus Challenger) |
| 0xFC08-0xFC0F | Acorn Speech chip (TMS5220) |
| 0xFC10-0xFC13 | Teletext adapter |
| 0xFC18-0xFC1F | IEEE-488 |
| 0xFC20-0xFC27 | BeebOPL |
| 0xFC28-0xFC2F | Joystick (some boards) |
| 0xFC30-0xFC3F | Rombox / User |
| **0xFC40-0xFC47** | **Hard disc (SCSI or IDE)** |
| 0xFC48-0xFC4F | Reserved |
| 0xFCF8-0xFCFF | Opus Challenger disc controller |
| 0xFCFF | JIM paging register, shared by every JIM-paged device (Music 5000/3000 use pages 0x3x and 0x5x of JIM, 0xFD00-0xFDFF; they claim nothing else in FRED) |

JIM (0xFD00-0xFDFF) is used as a 256-byte data window by some peripherals (e.g. Opus Challenger's sideways RAM), but the standard SCSI/IDE controllers do not use it -- all data transfer happens byte-by-byte through the data register at 0xFC40.

## Data Transfer Mechanism

Both SCSI and IDE use programmed I/O (PIO). There is no DMA on the BBC Micro's 1 MHz bus.

**SCSI**: The host adapter uses REQ/ACK handshaking. The CPU reads/writes one byte at a time from the data register (0xFC40), with the REQ signal in the status register indicating when the next byte is ready. ADFS typically transfers one 256-byte sector per SCSI READ/WRITE command, though the protocol supports up to 256 sectors per command.

**IDE**: The CPU reads/writes bytes from the data register after checking the DRQ (Data Request) bit in the status register. B-Em's implementation uses a split data register: even bytes at 0xFC40, odd bytes at 0xFC48 (16-bit word split across two 8-bit registers). BeebEm uses a simpler byte-streaming model through 0xFC40 alone.


## Hardware Reference Implementations

### BeebSCSI

[BeebSCSI](https://github.com/simoninns/BeebSCSI) is a hardware device that plugs into a real BBC Micro's 1 MHz bus and emulates an Acorn SCSI host adapter. It uses a Xilinx XC9572XL CPLD for bus interface logic and an Atmel AT90USB1287 AVR for SCSI command processing, with disc images stored on an SD card.

#### Architecture

The CPLD/AVR split maps cleanly onto how a software emulator should be structured:

- **CPLD** (bus interface): Address decoding for 0xFC40-0xFC44, data bus buffering, status register output, signal conditioning. In a software emulator, this is the memory-mapped I/O handler registered on the FRED page.
- **AVR** (SCSI logic): Phase state machine, command dispatch, sector read/write against SD card storage. In a software emulator, this is the SCSI controller class.

#### Register Interface

| Address | Read | Write |
|---------|------|-------|
| 0xFC40 | SCSI data bus | SCSI data bus |
| 0xFC41 | Status (MSG, BSY, REQ, I/O, C/D) | - |
| 0xFC42 | - | Assert nSEL (selection) |
| 0xFC43 | - | IRQ enable/disable |

The status register bit layout:

| Bit | Signal | Meaning |
|-----|--------|---------|
| 7 | CND | Command/Data |
| 6 | INO | Input/Output direction |
| 5 | REQ | Request (ready for transfer) |
| 4 | IRQ | Interrupt pending |
| 1 | BSY | Busy |
| 0 | MSG | Message phase |

All signals follow inverted (active-low) logic per the SCSI standard.

#### Image Format

BeebSCSI uses the same DAT+DSC format as B-Em and BeebEm, stored on a FAT-formatted SD card:

```
/BeebSCSI0/          <-- Jukebox directory 0 (up to 8 directories)
    scsi0.dat        <-- LUN 0 raw image (256-byte sectors)
    scsi0.dsc        <-- LUN 0 geometry descriptor (22 bytes)
    scsi0.cfg        <-- LUN 0 extended config (optional, text-based)
    ...
    scsi7.dat        <-- Up to 8 LUNs per directory
```

The CFG file is a BeebSCSI extension that provides richer geometry and SCSI mode page configuration than the 22-byte DSC format. It can specify custom sectors-per-track values (e.g. RLL drives with 61 SPT vs the standard MFM 33 SPT).

#### DSC Format Detail

The 22-byte DSC descriptor follows the SCSI MODE SELECT parameter format:

| Offset | Length | Field |
|--------|--------|-------|
| 0-3 | 4 | Mode Select Parameter List header |
| 4-11 | 8 | Extent Descriptor (block size at bytes 9-11, always 0x000100 = 256) |
| 12 | 1 | List format code (1) |
| 13-14 | 2 | Cylinder count (big-endian) |
| 15 | 1 | Head count |
| 16-17 | 2 | Reduced write current cylinder |
| 18-19 | 2 | Write pre-compensation cylinder |
| 20 | 1 | Landing zone position |
| 21 | 1 | Step pulse output rate code |

Sectors per track is fixed at 33 (ACB-4000 SuperForm 2:1 interleave). Total capacity = `cylinders * heads * 33 * 256` bytes. Maximum 512 MB per LUN (ADFS 21-bit LBA limit).

If no DSC file exists, geometry is auto-generated from the DAT file size.

#### Noteworthy Implementation Details

- **Auto-start behaviour**: Unlike the SCSI spec, ADFS never sends START/STOP UNIT commands. BeebSCSI auto-starts LUN 0 on mount and auto-starts any LUN on first READ/WRITE. A software emulator should do the same.
- **Jukebox switching**: Multiple sets of disc images can be swapped at runtime via a vendor-specific SCSI command (0x11), without rebooting the host. This is analogous to mounting different images via a gRPC service call.
- **8 LUNs per directory**: More than B-Em (4) or BeebEm (4). ADFS itself supports drives 0-7.
- **LaserDisc mode**: The same firmware also emulates a Philips VP415 LaserVision player for the BBC Domesday project, with vendor-specific F-Code commands. This demonstrates the versatility of the SCSI command framework.
- **Source quality**: Well-documented C code with clear phase state machine (`scsi.c`, ~2400 lines) and filesystem abstraction (`filesystem.c`, ~2000 lines). The SCSI state machine is the best reference for understanding the Acorn host adapter protocol.

### Pi1MHz

[Pi1MHz](https://github.com/dp111/Pi1MHz) uses a Raspberry Pi to simultaneously emulate multiple BBC Micro 1 MHz bus peripherals. Its SCSI emulation is derived from BeebSCSI and shares the same image formats.

#### Multi-Peripheral Emulation

Pi1MHz emulates many 1 MHz bus devices concurrently:

| Peripheral | Address | Notes |
|-----------|---------|-------|
| Hard disc (SCSI) | 0xFC40-0xFC44 | BeebSCSI-derived |
| Expansion RAM (JIM) | 0xFCFD-0xFCFF | Up to 992 MB on Pi 3B+ |
| Expansion RAM (byte) | 0xFC00-0xFC03 | 16 MB byte-mode |
| Music 5000/3000 | 0xFCFF paging, JIM pages 0x3x / 0x5x | Audio synthesis; register RAM behind the JIM window, not in FRED |
| SD card/FAT access | 0xFCD6 | File I/O buffer |
| Framebuffer | 0xFCA0 | HDMI video output |

This is relevant to Beebium's architecture: a pluggable FRED/JIM peripheral bus should support multiple devices at different address ranges simultaneously, just as Pi1MHz does.

#### SCSI Implementation

Pi1MHz's SCSI code is a port of BeebSCSI's AVR firmware to Linux/bare-metal Pi. The key differences:

- **More SCSI commands**: Adds INQUIRY (0x12), READ CAPACITY (0x25), REASSIGN BLOCKS (0x07), SEND DIAGNOSTIC (0x1D), and READ DEFECT DATA (0x37) beyond the BeebSCSI set.
- **Stateless polling**: Uses a non-blocking state machine registered via `Pi1MHz_Register_Poll()`. Each call advances the SCSI state by one step and returns. This is analogous to a `tick()` method in Beebium's architecture.
- **16 KB sector buffer**: Reduces SD card I/O overhead by buffering multiple sectors.
- **FatFS fast-seek**: Pre-builds cluster lookup tables for disc images to avoid worst-case FAT traversal during random access.
- **16 concurrent LUNs**: 0-7 for ADFS, 8-15 for VFS (LaserDisc). Each has independent file handles and geometry.

#### Image Format

Identical to BeebSCSI: DAT+DSC on a FAT filesystem, with optional CFG files for extended configuration. Same 512 MB per-LUN limit (ADFS constraint).

Same jukebox directory structure (`/BeebSCSI0/` through `/BeebSCSI7/`) with runtime switching via vendor-specific SCSI command.

## Implementation Complexity Comparison (Updated)

| Aspect | B-Em | b2 | BeebEm | BeebSCSI | Pi1MHz |
|--------|------|----|--------|----------|--------|
| SCSI source | ~900 lines | ~1000 lines | ~900 lines | ~2400 lines | ~2400 lines (ported) |
| IDE source | ~340 lines | - | ~260 lines | - | - |
| Image handling | (in SCSI) | ~340 lines | (in SCSI) | ~2000 lines | ~2000 lines (ported) |
| SCSI commands | 10 | 11 | 10 (+ 6 SASI) | 14 + vendor | 18 + vendor |
| Max LUNs | 4 | 4 | 4 | 8 | 16 |
| Image format | DAT+DSC | DAT+DSC | DAT+DSC | DAT+DSC+CFG | DAT+DSC+CFG |
| Electron support | - | Yes | - | N/A | N/A |
| Heritage | BeebEm port | Independent | Original | Original | BeebSCSI port |

The software emulators (B-Em, b2, BeebEm) implement the minimum needed to make ADFS work. b2's implementation is the most recent and has the cleanest architectural separation between SCSI protocol logic (`SCSI` class) and disc image handling (`HardDiskImage` class). The hardware projects (BeebSCSI, Pi1MHz) implement substantially more, driven by the need to work with a wider range of ADFS versions and third-party utilities that probe for additional SCSI capabilities.

For Beebium, the BeebSCSI SCSI state machine is the most authoritative reference for protocol completeness. b2's implementation is a useful additional reference for modern C++ idioms and clean separation of concerns.

## iSCSI as a Storage Backend

An interesting possibility for Beebium is supporting iSCSI (SCSI commands over TCP) as a storage backend, alongside conventional disc image files. This would allow a Beebium emulator instance to connect to real or virtual storage over the network -- a capability no other BBC Micro emulator offers.

### Why iSCSI Fits Beebium's Architecture

Beebium's multi-process, gRPC-based architecture already separates the emulation core from frontends via network protocols. Extending this philosophy to storage is a natural step:

```
6502 ADFS driver
  |
emulated Acorn SCSI host adapter (0xFC40-0xFC43)
  |
virtual SCSI target interface
  |
  +-- Image backend (DAT file)        <-- default, safe, portable
  +-- iSCSI backend (TCP to target)   <-- optional, for real/networked storage
  +-- Debug backend (logs commands)    <-- for development
```

The critical design principle: **the emulator speaks "abstract SCSI target", not "real storage"**. The Acorn SCSI host adapter emulation and bus phase state machine are always inside Beebium. Only the storage backend varies.

### What iSCSI Is (and Isn't)

iSCSI is not raw SCSI hardware passthrough. It encapsulates SCSI commands (CDBs) inside TCP packets. This means:

- No special hardware needed -- it works over standard networking
- No privileged access required (unlike raw `/dev/sdX` access)
- The emulator never touches physical SCSI buses
- Authentication, retries, and transport are handled by the iSCSI layer

This is fundamentally different from trying to pass through raw SCSI commands to physical devices, which would require platform-specific privileged APIs and is not practically viable.

### Host OS iSCSI Support

| Platform | iSCSI Initiator | Status |
|----------|----------------|--------|
| Linux | `open-iscsi` (kernel) | Excellent; block devices appear automatically |
| Windows | Built-in initiator | Mature, reliable |
| macOS | No native initiator | Third-party only (ATTO, globalSAN); workable but not seamless |

Two implementation approaches:

1. **OS-level initiator**: The host OS connects to the iSCSI target and presents a block device. Beebium opens the block device as a raw file. Simple but requires OS-level setup and may need elevated privileges.

2. **User-space iSCSI in Beebium**: Beebium implements a minimal iSCSI initiator directly, translating the emulated SCSI commands into iSCSI PDUs over TCP. More self-contained but more implementation work. Since the Acorn SCSI command set is tiny (READ, WRITE, REQUEST SENSE, and a handful of others), the iSCSI framing needed is minimal.

### Use Cases

- **Shared storage between emulator instances**: Multiple Beebium servers could mount the same iSCSI target, emulating networked BBC Micros sharing a fileserver's hard disc
- **Large storage pools**: iSCSI targets can be backed by anything -- ZFS volumes, cloud block storage, NAS appliances -- far exceeding what local disc images offer
- **Classroom/museum setups**: A single iSCSI target server could serve pre-configured hard disc images to many Beebium instances
- **Real hardware integration**: Users with actual SCSI drives could expose them via an iSCSI target, keeping the "danger" outside Beebium

### Architectural Recommendation

iSCSI support should be an optional backend behind the same virtual SCSI target interface used by disc images. The interface boundary is the SCSI CDB: the host adapter emulation produces CDBs, and the backend consumes them. Whether the backend reads from a DAT file or forwards over TCP is invisible to the emulated BBC Micro.

This could be exposed via gRPC as a target type when mounting a hard disc:

```
// Mount a disc image (default)
MountHardDisc(drive: 0, url: "file:///path/to/scsi0.dat")

// Mount an iSCSI target
MountHardDisc(drive: 0, url: "iscsi://target-host/iqn.2026-03.org.beebium:disc0")
```

### Priority

iSCSI is not needed for an initial hard disc implementation. The recommended approach is:

1. Implement the virtual SCSI target interface with a disc image backend (DAT+DSC)
2. Ensure the interface boundary is clean (CDB in, data out)
3. Add iSCSI as a second backend later, once the SCSI emulation is proven

The important thing is to design the target interface with this future in mind, so that adding iSCSI doesn't require restructuring the SCSI emulation layer.

## BBC Master AIV and LaserDisc Support

The BBC Master AIV (Advanced Interactive Video) system used SCSI to control a Philips VP415 LaserDisc player for the BBC Domesday Project. This is relevant to Beebium's SCSI design because the VP415 appears as a SCSI target on the same bus as hard discs, using the same host adapter protocol but with additional vendor-specific commands.

### AIV Host Adapter

The AIV SCSI Host Adapter is an **internal** version of the standard Acorn SCSI Host Adapter, connected to the Master 128's internal 1 MHz bus (PL12 connector) rather than the external 1 MHz bus. It uses the same register addresses (0xFC40-0xFC43) and the same bus phase protocol, but has several hardware differences:

| Aspect | Standard (External) Adapter | AIV (Internal) Adapter |
|--------|---------------------------|----------------------|
| Bus levels | TTL (external 1 MHz bus) | CMOS (internal 1 MHz bus) |
| Address decoder | 74LS138 / 74HCT138 | 74HC138 |
| Data bus inversion | Hardware (IC13 74LS240 inverts outbound data) | Software (VFS performs inversion) |
| IRQ flag polarity | Non-inverted | Inverted (missing inverter on board) |
| Status bit 2 | Grounded (unused) | Connected to SCSI RESET pin |
| Address bus width | 8-bit | 4-bit |

The cards are not electrically interchangeable (CMOS vs TTL), but the SCSI protocol is identical. The software differences (data bus inversion, IRQ polarity) are handled by the VFS ROM rather than by hardware.

### Design Implication for Beebium

The AIV adapter differences mean the host adapter emulation should not hard-code data inversion or IRQ polarity. These should be configurable (or handled entirely by the ROM software, as on real hardware). Since both adapters present the same register interface at the same address, a single SCSI host adapter implementation should suffice -- the AIV quirks are compensated for in the VFS ROM, not in the adapter hardware.

### VFS (Video Filing System) ROM

VFS version 1.70 is essentially a read-only variant of ADFS with extensions for VP415 control. It provides:
- Standard BBC filing system interface for reading data from the LV-ROM disc
- F-code commands transmitted to the VP415 via SCSI Group 6 vendor-specific commands
- Software data bus inversion (compensating for the AIV hardware difference)
- Software IRQ polarity inversion

### VP415 on the SCSI Bus

The VP415 appears as a SCSI target alongside any hard discs. The bus topology uses no arbitration (the simple Acorn host adapter does not support it):

- **Host (initiator) ID**: 1 (0x02 on bus)
- **ADFS hard discs**: LUN 0-3
- **VFS LaserDisc**: LUN 0-7

The VP415 is addressed via standard SCSI READ (0x08) for data retrieval and Group 6 vendor-specific commands (0xC8/0xCA) for F-code control of the player.

### VP415 SCSI Commands

Beyond the standard SCSI commands used for hard discs, the VP415 requires two additional Group 6 (vendor-specific) commands:

| Opcode | Command | Direction | Purpose |
|--------|---------|-----------|---------|
| 0xC8 | Read F-Code | DATA IN | Read buffered response from VP415 |
| 0xCA | Write F-Code | DATA OUT | Send F-code command to VP415 |

F-code data is transferred in 256-byte buffers. Commands are ASCII strings terminated by CR (0x0D), null-padded. The VP415 F-code set provides comprehensive playback control:

| Category | Example F-Codes |
|----------|----------------|
| Playback | `N` (play), `O` (reverse), `*` (halt), `/` (pause) |
| Speed | `W` (fast forward), `Z` (fast reverse), `U`/`V` (slow motion) |
| Navigation | `FnnnnN` (go to frame, play), `FnnnnR` (go to frame, halt) |
| Audio | `A0`/`A1` (channel 1), `B0`/`B1` (channel 2) |
| Video | `E0`/`E1` (video off/on), `VP1`-`VP5` (overlay modes) |
| Status queries | `?F` (frame number), `?D` (disc status), `?P` (player status) |
| System | `:` (reset), `'` (eject), `,0`/`,1` (standby/load) |

BeebSCSI (designed by Simon Inns, who also leads the Domesday86 recovery project) implements the full VP415 F-code set in its LV-DOS emulation mode, making it the authoritative reference for this protocol.

### Implications for Beebium's SCSI Architecture

The VP415 support reinforces several design decisions:

1. **The SCSI target interface must be generic**, not hard-disc-specific. A VP415 target handles Group 6 commands and F-codes rather than sector read/write. The CDB dispatch should be extensible.

2. **Multiple target types on one bus**: The SCSI bus must support mixed target types (hard disc + LaserDisc player) simultaneously, each with their own command handling.

3. **Group 6 vendor-specific commands must not be rejected**: The command dispatcher should pass unrecognised opcodes to the target implementation rather than returning CHECK CONDITION. Different targets handle different command groups.

4. **The host adapter is the stable part**: Both the standard and AIV adapters present the same register interface. The adapter emulation should be a thin layer; all intelligence lives in the targets.

5. **BeebSCSI is the single best reference**: Simon Inns' implementation handles both ADFS hard discs and VP415 LaserDisc through the same SCSI bus abstraction, which is exactly the architecture Beebium should follow.

### References

- [Domesday86: Acorn BBC Master AIV](https://www.domesday86.com/?page_id=67)
- [Domesday86: Philips VP415](https://www.domesday86.com/?page_id=316)
- [Domesday86: VFS ROM](https://www.domesday86.com/?page_id=70)
- [Domesday86: Acorn AIV SCSI Adapter](https://www.domesday86.com/?page_id=64)
- [BeebSCSI source (scsi.c, fcode.c)](https://github.com/simoninns/BeebSCSI)
- [Acorn AIV SCSI Adapter Card KiCad reproduction](https://github.com/simoninns/Acorn-AIV-SCSI-Adapter-Card)
- [VP415 Emulator](https://github.com/simoninns/VP415Emu)

## Pre-formatted Hard Disc Images

[Jon Ripley's BBC Micro Hard Drives page](https://jonripley.com/8bit/HardDrives/) provides 22 blank, pre-formatted ADFS hard disc images ranging from 2 MB to 512 MB. These are raw ADFS old-map format (256-byte sectors, "Hugo" root directory marker) distributed as `.adl` files inside zip archives. They contain no software -- just an empty root directory and free space map.

These images are not directly compatible with existing emulators due to naming conventions (emulators expect `scsiN.dat` + `scsiN.dsc` pairs), but the raw disc data is identical in format. Renaming `2MbHDD.adl` to `scsi0.dat` and allowing the emulator to auto-generate a DSC geometry file is sufficient.

The images are exact power-of-two megabyte sizes, which are not evenly divisible by the standard SCSI geometry stride (33 sectors/track * heads * 256 bytes). This wastes a few KB at the end of the image but is otherwise harmless, since the ADFS free space map records the actual disc size in sectors independently of the CHS geometry.

These images are useful for testing a Beebium SCSI implementation without needing to format discs from within the emulator. They cover the full range of ADFS-supported sizes up to the 512 MB limit.

## Beebium SCSI Implementation

Beebium implements full SCSI hard disc support via the peripheral extension framework. The implementation is tested end-to-end: a Model B+ boots ADFS 1.30, detects the SCSI adapter, reads a real ADFS-formatted hard disc image, and `*CAT` displays the directory listing.

### Architecture

```
AcornScsiHostAdapter (PeripheralExtension + OneMHzBusDevice)
  attaches_to = {"1mhz-bus"}, provides = {"scsi"}
  registers at 0xFC40-0xFC43 on the FRED page
  │
  ├─ ScsiBus (protocol state machine)
  │    phases: BUS_FREE → SELECTION → COMMAND → DATA_IN/DATA_OUT → STATUS → MESSAGE_IN
  │    20 SCSI commands, register interface matching Acorn host adapter
  │
  └─ ScsiTargetRegistry (8 SCSI IDs)
       └─ ScsiHardDisc (ScsiTarget)
            └─ HardDiskImage (DAT+DSC file I/O)
```

### Components

| Component | Location | Purpose |
|-----------|----------|---------|
| `ScsiBus` | `src/extensions/acorn-scsi/ScsiBus.hpp/.cpp` | Bus protocol state machine |
| `ScsiTarget` | `src/extensions/acorn-scsi/ScsiTarget.hpp` | Target interface |
| `ScsiHardDisc` | `src/extensions/acorn-scsi/ScsiHardDisc.hpp/.cpp` | Hard disc SCSI command handler |
| `HardDiskImage` | `src/extensions/acorn-scsi/HardDiskImage.hpp/.cpp` | DAT+DSC file I/O |
| `ScsiTestDevice` | `src/extensions/acorn-scsi/ScsiTestDevice.hpp/.cpp` | Configurable test target |
| `ScsiTargetRegistry` | `src/extensions/acorn-scsi/ScsiTargetRegistry.hpp` | Target slot management |
| `AcornScsiHostAdapter` | `src/extensions/acorn-scsi/AcornScsiHostAdapter.hpp/.cpp` | Extension wrapper |
| `ScsiHostAdapterService` | `src/extensions/acorn-scsi/ScsiHostAdapterService.hpp` | gRPC service |
| `ScsiConstants` | `src/extensions/acorn-scsi/ScsiConstants.hpp` | Protocol constants |

### SCSI Commands Supported

All 20 commands from the union of B-Em, b2, BeebEm, BeebSCSI, and Pi1MHz. See the SCSI Command Sets table above for the full list.

### Disc Geometry

- 256-byte sectors (Acorn SCSI standard, not standard 512)
- 33 sectors per track (ACB-4000 SuperForm 2:1 interleave)
- Variable heads and cylinders from DSC descriptor
- Maximum 512 MB per LUN (ADFS 21-bit LBA limit: 2,097,151 sectors)

### Protocol Notes

Key implementation details discovered during ADFS compatibility testing:

- **REQ always set**: The status register always has REQ=1 (0x20), even in BusFree. Both b2 and BeebEm hardcode this. The ADFS SCSI driver locks up without it.
- **Selection triggered by REG_SELECT**: Writing the target ID to the data register (reg 0) does NOT trigger selection. Selection occurs when the host writes to the select register (reg 2). This prevents false selection during ADFS's hardware probe which writes test patterns (0x5A, 0xA5) to the data register.
- **Data register read-back**: In BusFree and Selection phases, reading the data register returns the last value written. ADFS uses this for hardware detection.
- **REG_SELECT is control-only**: The value written to reg 2 must NOT be injected into the CDB buffer. It is purely a control signal that triggers the BusFree→Selection→Command transition.

### Running Beebium with SCSI Hard Disc

The SCSI adapter and hard disc target are **plugin extensions** (no BBC Micro model except the Master AIV had a built-in SCSI adapter). Both must be loaded explicitly via `--extension-dir` and their respective CLI flags.

**Model B+ with one hard disc:**

```bash
beebium-model-b-plus start \
    --sideways 11:rom:acorn-adfs_1_30.rom \
    --extension-dir build/src/extensions \
    --acorn-scsi \
    --scsi-hdd 0:/path/to/scsi0.dat
```

This:
1. Replaces the default DFS ROM (slot 11) with ADFS 1.30 (the SCSI version for BBC B/B+)
2. Loads the Acorn SCSI Host Adapter plugin (registers at 0xFC40-0xFC43)
3. Loads a SCSI hard disc target at SCSI ID 0 with the specified image

**Model B+ with two hard discs:**

```bash
beebium-model-b-plus start \
    --sideways 11:rom:acorn-adfs_1_30.rom \
    --extension-dir build/src/extensions \
    --acorn-scsi \
    --scsi-hdd 0:/path/to/system.dat \
    --scsi-hdd 1:/path/to/data.dat
```

ADFS sees these as drives 0 and 1. Floppy drives are at drives 4-5.

**Model B:**

```bash
beebium-model-b start \
    --sideways 5:rom:acorn-adfs_1_30.rom \
    --extension-dir build/src/extensions \
    --acorn-scsi \
    --scsi-hdd 0:/path/to/scsi0.dat
```

Uses slot 5 (IC88 socket) for the ADFS ROM.

**With named instances (advanced):**

```bash
--acorn-scsi id=my-scsi \
--scsi-hdd 0:/path/to/drive.dat:adapter-id=my-scsi:id=boot:label=System Disc
```

### Preset File Example

```json
{
    "name": "Model B+ with SCSI",
    "model": "model-b-plus",
    "extensions": [
        {"name": "acorn-scsi"},
        {"name": "scsi-hard-disc", "config": {"scsi-id": "0", "image": "scsi0.dat"}}
    ]
}
```

### Creating and Using Disc Images

**Using existing images:** DAT+DSC file pairs from B-Em, BeebEm, or BeebSCSI work directly. Copy them to a convenient location and specify the DAT filepath on the `--scsi-hdd` flag.

**Creating blank images:** Use `HardDiskImage::create()` programmatically to create a new zero-filled disc image with specified geometry. The disc is "unformatted" -- use ADFS's formatting utility on the 6502 side to initialise the filesystem.

**Pre-formatted images:** [Jon Ripley's BBC Micro Hard Drives page](https://jonripley.com/8bit/HardDrives/) provides 22 blank ADFS-formatted images from 2 MB to 512 MB. Rename from `.adl` to `.dat` and auto-generate geometry.

### Test Coverage

108 SCSI tests across all layers:
- 15 ScsiTestDevice (configurable responses, recording)
- 36 ScsiBus protocol (all phases, 20 commands, multi-block, F-codes, multi-target)
- 5 ScsiTargetRegistry
- 14 HardDiskImage (create, open, read/write, geometry, format, LBA validation)
- 17 ScsiHardDisc (all commands, error paths, sense data lifecycle)
- 8 ADFS image tests (real BeebEm disc image, Hugo signature, geometry)
- 5 ADFS protocol compatibility (probe patterns, selection handshake)
- 5 AcornScsiHostAdapter integration (through hardware memory map)
- 3 ScsiHostAdapterService gRPC
- 1 End-to-end boot test (Model B+ + ADFS 1.30 + SCSI hard disc + `*CAT`)

## Peripheral Extension Architecture

The peripheral extension framework supporting the SCSI adapter (and all future 1 MHz bus peripherals) is documented in [Peripheral Extension Framework](peripheral-extension-framework.md).
