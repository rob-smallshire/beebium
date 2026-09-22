# Watford Electronics ROM/RAM Board

Transcription of the Watford Electronics ROM/RAM Board manual
(`WE_RAMROMBoard.pdf`, 14 pages). The source PDF carries a text layer, so this
is a lightly cleaned transcription (obvious scanning typos corrected; all
addresses, commands, and technical detail preserved verbatim). Software
copyright (C) 1985 Watford Electronics / Ian Smith; the SFS (C) 1985 Watford
Electronics / Andy Bray.

> Watford Electronics, 250 High Street, Watford, Herts., England.
> Tel: Watford (0923) 40588 / (0923) 37774

---

## ROM/RAM Board Installation

The first stage in fitting the ROM/RAM board is to turn off the BBC and remove
the mains plug. Remove the cover (four case screws marked 'FIX'). If a ROM board
is already fitted, remove it first. If a Watford 32k RAM Card or Delta Card is
fitted, temporarily remove it from the 6502 socket -- it will later plug into
the equivalent socket provided on the ROM/RAM board.

Orientation convention: rear = north, front = south, left = west, right = east.

1. Remove the 6502 CPU from IC1 (lever it out carefully, alternating ends).
2. Locate link **S21** (four posts in a square). Remove the connecting link from
   the southern-most east-west pair and keep it. Fit the twisted trailing pair
   from the ROM/RAM board: the **W** (west) wire to the west post, the **E**
   (east) wire to the east post. Orientation matters.
3. Move the two power leads (`0V`, black; `VCC2`/`VCC 5V`, red) from the BBC
   board to the ROM/RAM board spades, then run the board's own `0V` and `S4/OUT`
   leads back to the vacated BBC spades. Net result: the original PSU wires feed
   the ROM/RAM board, and new wires feed the original BBC spades.
4. Plug the board squarely into the vacated 6502 socket IC1. If the rear-right
   spades foul the board, gently bend them down.
5. Refit the 6502 into the socket marked **6502A** on the board (or plug the 40-way
   ribbon of a Watford 32k RAM / Delta Card here).

On power-up: a continuous buzz means the board is not seated / wires wrong;
`Language?` on screen means the S21 flying lead is reversed -- power off, correct
it, power on.

---

## The Printer Buffer

Loaded from the supplied utilities disc menu. Once installed it responds to `*`
commands:

```
>*HELP
Watford Electronics Printer Buffer
BUFFER
OS 1.20
>*HELP BUFFER
Watford Electronics Printer Buffer
  BUFFON
  BUFFOFF
  PURGE
  PURGEON
  PURGEOFF
OS 1.20
```

With the printer buffer active, characters bound for the printer are directed
through the buffer software; if the printer is not ready the character is saved
into sideways RAM until it is. **One bank of sideways RAM is required for the
printer buffer.** If the bank normally used for CMOS RAM is used, the link
**RD-S1** must be in place, or the buffer will not work correctly.

- `*BUFFOFF` -- deactivate the buffer (default state is inactive).
- `*BUFFON` -- activate the buffer.
- `*PURGE` -- abort printing and discard buffer contents (unrecoverable).
- `*PURGEON` -- pressing Escape acts as `*PURGE` (default when the buffer is on).
- `*PURGEOFF` -- disable purge-on-Escape (useful under a word processor that uses
  Escape to toggle modes).

---

## General Information

The BBC micro can address sixteen sideways ROMs. Normally only four ROM sockets
are available; with the ROM/RAM board fitted this rises to eight ROM sockets,
plus up to eight banks of sideways RAM. Sideways RAM can hold infrequently used
ROMs, act as a silicon disc, or act as a printer buffer.

### Bank map

- **Banks 0-7** -- dynamic sideways RAM (DRAM) on the ROM/RAM board. Minimum
  configuration 32k (two banks); up to 128k (eight banks). **DRAM contents are
  lost at power-off.**
- **Banks 8-11** -- the four original ROM sockets under the keyboard. ROMs there
  may remain, but any ROM on the ROM/RAM board takes **higher priority**. It is
  recommended the BASIC ROM be moved to socket 15 of the board.
- **Banks 12, 13, 15** -- ROM sockets on the ROM/RAM board. Behave like the
  original sockets but take higher priority.
- **Bank 14** -- split into two sockets (14L, 14H), each taking a ROM or an 8k
  static RAM. Use two 8k devices (EPROM 2764; static CMOS RAM 6264). With the
  battery backup fitted, static-RAM contents survive power-off. See link **S3**.

### The write-select latch (&FF30)

In addition to the normal ROM-select latch at **`&FE30`**, the board provides an
**additional latch to control write operations** to the RAM. It differs in form:
a write is made to **`&FF30` plus the socket number**, and the *value written is
irrelevant* -- only the address matters.

- To select socket 5 for writes from BASIC: `?&FF35=0`.
- In assembler, to select socket `&D`: `LDX #&D : STA &FF30,X`.
- **Do not** use `!&FF30=0` -- that writes four bytes (`&FF30..&FF33`) and would
  leave socket 3 selected, giving strange results. Use single-byte writes only.

Having selected a write socket, **all subsequent writes to `&8000-&BFFF` are
directed to that socket** (independently of the `&FE30` read-select). This lets
you `*LOAD` or assemble straight into a chosen RAM bank.

- **Temporary write-protect:** `?&FF38=0` selects socket 8 (a ROM) for writes;
  since it is ROM, no RAM is affected by write attempts. This is only temporary
  -- other software can reselect a socket.
- **Permanent write-protect:** remove link **S2** (left side of board), or open a
  switch wired to it.

### Links

- **S1** (left, middle) -- read-protect link for socket 14. Removed, socket 14
  "vanishes" (yields a single constant value). Recovery for battery-backed RAM
  holding software that hangs the machine: remove S1, press Break, enter BASIC,
  and `?&FF3E=0:?&8007=0` (selects socket 14 for writes and zeroes the ROM
  copyright pointer, disabling the ROM).
- **S2** (left) -- permanent write-protect for all sockets (see above).
- **S3** -- PCB track between the upper two of three pads. Selects whether sockets
  14L/14H are two 8k devices (default, centre-to-rear) or a single 16k device in
  socket 14H.

### Battery backup

A PCB-mounting NiCad battery (2.6V) soldered into the three holes to the right of
the 6502A socket. Charging circuitry is on-board; a few hours fully charges a
flat cell, which then preserves the CMOS RAM for several months. Typical current
for a fully populated board is 0.5A.

---

## The Silicon Filing System (SFS)

The major utility supplied is the **SFS (Silicon Filing System)**, a modified
Watford DFS that treats sideways RAM in banks 0-7 as a silicon disc. When active,
all operations are directed to sideways RAM, not to the disc. Fast copy utilities
move data between disc/Econet and the SFS.

```
>*HELP
Watford Electronics SFS 1.00
   RFS
   FILES
   SPACE
   UTILS
OS 1.20
>*HELP SFS
Watford Electronics SFS 1.00
   ACCESS <afsp> (L)     COMPACT              DELETE <fsp>
   DESTROY <afsp>        DIR (:<drive>.) <dir>  DRIVE <drv>
   ENABLE                INFO <afsp>          INIT (-)<ROM list>
   LIB (:<drive>.) <dir> RENAME <old> <new>   TITLE <title>
   WIPE <afsp>           WORK <fsp>
OS 1.20
>*HELP UTILS
Watford Electronics SFS 1.00
   BUILD <fsp>   CFSDISK <afsp>   CTSDISK <afsp>   DUMP <fsp>
   LIST <fsp>    SILICON          RLOAD <ROM no.> <fsp>
   TIDY         TYPE <fsp>
```

Loading directly (socket 14 / `&E` is recommended for the SFS):

```
?&FF3E=0
*LOAD SFS 8000
```

then press Break to initialise. On first call the equivalent of `*INIT` runs.
Only one silicon disc is implemented; drive numbers are accepted but ignored.

Notable commands:

- `*OPT`/`*OPT0,0` issues `*OPT1,0` and `*OPT3,0`; `*OPT0,1` issues `*OPT1,1` and
  `*OPT3,1`. `*OPT3,0` selects the Watford `*LIB`/`*DIR` extensions; `*OPT3,1`
  makes the SFS behave as a normal Acorn DFS.
- Most commands mirror their DFS counterparts (drive references have no effect).
- `COMPACT` -- gathers free space without corrupting user memory.
- `ENABLE` -- as DFS; when omitted before `*DESTROY`/`*INIT`, the SFS prompts
  `Go (Y/N)?`.
- `INIT (-)<ROM list>` -- reclaims sideways RAM not marked in use in the sideways
  ROM table at **`&2A1`** and formats the silicon disc (e.g. `Silicon disc
  cleared. Set up with 128k`). A socket list (hex, no spaces) restricts the
  sockets used (`*INIT 123E`). A leading `-` treats the list as sockets to
  ignore. Sockets that are not RAM / are an active ROM are reported and skipped.
- `TITLE` -- default title `Silicon Disc`.
- `CFSDISK`/`CTSDISK` -- copy silicon-disc <-> current filing system.
- `RLOAD <ROM no.> <fsp>` -- load a ROM image on disc into the given RAM socket.
  Errors: `Socket in use.`, `Not RAM.`.
- `SILICON` -- select the SFS (like `*DISC` selects the DFS). Selected on Break if
  highest-priority filing system, or by holding `S` during Break. `*SILICON I`
  reinitialises.

---

## Devices (from the schematic, P.A. Van Ek, 22-02-1988)

- Read/write select: two `74LS75` quad latches (read select from `&FE30` data
  bus; write select from `&FF3X` low address nibble A0-A3).
- Bank decode: `74LS138` decoders drive the eight DRAM bank chip-selects.
- DRAM: 16 x HM4416 (16Kx4), High/Low pairs -> eight 16K banks (banks 0-7).
- Bank 14: two HM6264 (8Kx8) static RAM or 27128 ROM sockets (14 High R/W, 14 Low
  R/W), with a 3.6V NiCad + 1N4148 for battery backup.
- ROM sockets: 27128 (16K) for ROM 12, ROM 13, ROM 15.
- Links: S21 (motherboard tap), S2 (write-protect), S1 (socket-14 read-protect),
  S3 (bank-14 2x8K vs 1x16K).
