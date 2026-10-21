# Intel 8271 floppy disc controller: comparative study and proposed design

Status: Phase 1 study for #87. No implementation yet; the design at the end is a
proposal for review.

This document compares five emulations of the Intel 8271 as fitted to the BBC
Model B, says which behaviour the real chip has wherever the evidence settles it,
and proposes how Beebium should add the chip behind its existing disc controller
socket.

## 1. Sources

### 1.1 Emulators read

| Emulator | File | Lines | Character |
|---|---|---|---|
| beebjit | `/Users/rjs/Code/beebjit/intel_fdc.c` (+ `make_8271_rom.c`, `bbc.c`, `disc_drive.c`) | 2144 | Models the real chip's internal register file (R0-R31) and control flow from a disassembly of its program ROM; not instruction-level. Primary oracle. |
| jsbeeb | `/Users/rjs/Code/jsbeeb/src/intel-fdc.js` | 1717 | Near-literal translation of beebjit, including beebjit's 2021 fixes. Not an independent model. |
| MAME | `/Users/rjs/Code/mame/src/devices/machine/i8271.cpp` (+ `bus/bbc/fdc/acorn.cpp`) | 1726 | Independent, datasheet-driven, bit-level "live" FM engine. Default `bbcb` FDC. |
| BeebEm | `/Users/rjs/Code/beebem-mac/Src/Disc8271.cpp` | 3093 | Pragmatic, sector-image based, many game-specific hacks for FSD images. |
| B-Em | `/Users/rjs/Code/b-em/src/i8271.c` | 568 | Minimal front end over a 1770-shaped media API. |

B2 has no 8271 and is not a reference here.

Detailed per-emulator notes with file:line citations were produced during the
study; the citations below are to the emulator sources themselves.

### 1.2 Hardware evidence

- **beebjit's hardware tests.** `make_8271_rom.c` builds a 6502 test OS ROM whose
  assertions were checked on a real Model B: power-on register values, status
  sequences around command and parameter writes, latched not-ready, a
  format / read-ID / write / read-back cycle. These are the strongest
  host-protocol facts available, and the proposed unit tests reuse them
  (section 9).
- **The write-up.** Chris Evans, "Reverse engineering a forgotten 1970s Intel
  dual core beast: 8271" (scarybeastsecurity.blogspot.com, November 2020):
  die and program-ROM analysis. It establishes the register file (external
  RESULT = R22, STATUS = R23, COMMAND = R31, PARAMETER = R30), the parameter
  count rule, the bad-track integer overflow, the command-while-busy side
  effects, that seek uses millisecond busy loops, and that the BBC drives the
  8271 at 4 MHz.
- **The Stardot thread** (viewtopic.php?f=3&t=21053): DFS 0.9 issues three
  SPECIFY and one WRITE SPECIAL REGISTER at init; the "disc-eating" test mode;
  Acorn's schematic drives CS and DACK low together, which the Intel datasheet
  forbids (left unanswered in the thread).
- **Acorn's own DFS 1.20 source.** The DNFS 3.00 source release is on disc at
  `/Users/rjs/Code/acorn_1770_dfs_disassembly/submodules/AcornDNFSv300/src/`
  (DFS 1.20 + NFS 3.60 + Tube host 1.10). `DOS01` and `DOS03` give the exact
  register usage of DFS 1.20 (section 2.6). This was not in the brief and is
  the best evidence of what DFS needs.
- **No datasheet locally.** `docs/manuals_text/` holds only the ADFS user guide;
  the 8271 datasheet, the Advanced User Guide and the Acorn DFS guide are not
  transcribed. Claims attributed to the datasheet below are marked
  **[DS, unverified]** and should be checked when a transcript is available.

## 2. The chip as fitted to the Model B

This section collects what is settled, so the comparison in section 4 can
concentrate on disagreements.

### 2.1 Wiring and address decode

The 8271 occupies &FE80-&FE9F, decoded on A2..A0 only (A3, A4 ignored, so the
eight locations mirror four times). A2 = 0 selects the chip (CS); A2 = 1 asserts
DACK, which reaches the data register with A0/A1 ignored.

| Offset (addr & 7) | Read | Write |
|---|---|---|
| 0 | status | command |
| 1 | result | parameter |
| 2 | byte counter MSB (undocumented, beebjit observation) | reset |
| 3 | byte counter LSB (undocumented, beebjit observation) | nothing |
| 4-7 | data (DACK) | data (DACK) |

&FE80-&FE9F is a 2 MHz (unstretched) region; Beebium's `BusStretching.hpp`
already treats it so. The 8271 is clocked at 4 MHz on the Beeb (MAME's
`DERIVED_CLOCK(1,2)` of the 8 MHz slot; the write-up).

The INT pin drives the 6502 NMI. DRQ is not wired. Only non-DMA mode works, so
every data byte is moved by an NMI handler, and SCAN (which needs DMA) is
unusable on the BBC.

There is no drive control latch on the 8271 board. Drive select, side select,
motor and head load all come from the 8271's own drive control output port
(special register &23): SELECT1 &80, SELECT0 &40, SIDE &20, LOW HEAD CURRENT
&10, LOAD HEAD &08, DIRECTION &04, STEP &02, WRITE ENABLE &01. On the BBC,
LOAD HEAD is the drive motor ("writing &48 turns on the motor", the write-up).

### 2.2 Registers and the command protocol

- Writing the command register raises BUSY (&80) and, observed on hardware,
  RESULT READY (&10): status reads &90 straight after a command write
  (beebjit test ROM; BeebEm independently comments "observed on beeb").
- Parameters: the count is `(cmd & 0x18) ? (cmd & 3) : 5` (from the chip's ROM;
  confirmed on hardware by the write-up: incrementing a one-parameter opcode
  makes the chip demand two). Writing a parameter clears RESULT READY.
  Parameters land in R7 downwards. Every command write first sets R5 = R4 = 1,
  which is what makes the "single 128-byte record" opcodes work.
- Drive select is bits 7:6 of the command byte (&40 drive 0, &80 drive 1),
  applied when the command starts (after the last parameter).
- Reading the result register clears RESULT READY and INT. Reading or writing
  the data register clears INT and the data request bit.

### 2.3 Status register

| Bit | Name | Behaviour (beebjit, hardware-tested where noted) |
|---|---|---|
| &80 | BUSY | Set on command write; cleared at completion. |
| &40 | COMMAND FULL | Never observed by software (acceptance is effectively immediate). |
| &20 | PARAMETER FULL | Never observed by software. DFS still polls it before each parameter (`DOS03` `DSKPAR`). |
| &10 | RESULT READY | Set on command write (hw), at completion, and on READ DRIVE STATUS / READ SPECIAL REGISTER; cleared by result read or any parameter write (hw). |
| &08 | INT | Per data byte and at completion; drives NMI. |
| &04 | NON-DMA DATA REQUEST | Per data byte only, never at completion. DFS's NMI handler uses it to tell a data NMI from a completion NMI. |
| &02, &01 | - | Always 0. **DFS 1.20 relies on this**: `DOS06` tests `FE80 AND 3` and treats non-zero as "no 8271" (with a WD1770 board &FE80 reads &FF). |

### 2.4 Commands

| Opcode | Command | Params |
|---|---|---|
| &00 / &04 | Scan data / scan data and deleted | 5 (unusable on the BBC: needs DMA) |
| &0A / &0B | Write data 128 / variable | 2 / 3 |
| &0E / &0F | Write deleted data 128 / variable | 2 / 3 |
| &12 / &13 | Read data 128 / variable | 2 / 3 |
| &16 / &17 | Read data and deleted 128 / variable | 2 / 3 |
| &1B | Read ID | 3 (track, 0, count) |
| &1E / &1F | Verify data and deleted 128 / variable | 2 / 3 |
| &23 | Format | 5 (track, GAP3, size/count, GAP5, GAP1) |
| &29 | Seek | 1 |
| &2C | Read drive status | 0 |
| &35 | Specify | 1 + 3 |
| &3A | Write special register | 2 |
| &3D | Read special register | 1 |

The size/count byte is size code in bits 7:5 (128 << code bytes) and count in
bits 4:0 (0 means 32). The internal command is `(cmd & 0x3C) >> 2`; other
opcodes are decoded by the same rule, so e.g. &14 behaves as a read with a
different parameter count. Opcode groups 9 and 12 (&24-&27, &30-&33) are unused
and their real behaviour is unknown.

### 2.5 Special registers

| Reg | Meaning |
|---|---|
| &06 | scan sector / current sector |
| &0D, &0E, &0F | step rate, head settle, head load (low nibble) + unload count (high nibble); written by SPECIFY &0D |
| &10, &11, &12 | drive 0 (surface 0) bad track 1, bad track 2, current track; SPECIFY &10 |
| &13, &14 | scan count LSB/MSB (beebjit: the sector byte counter) |
| &17 | mode: bit 0 non-DMA, bit 1 single actuator. Shares R23 with the internal status (beebjit). |
| &18, &19, &1A | drive 1 (surface 1) bad tracks, current track; SPECIFY &18 |
| &22 | drive control input (read only) |
| &23 | drive control output |

Per the write-up, READ/WRITE SPECIAL REGISTER can reach any of R0-R31 (and the
bit processor's MMIO registers at &20-&27), mirrored across 0-255; beebjit
implements exactly that.

### 2.6 What Acorn DFS actually uses

From the DFS 1.20 source (`DOS03` `CMDTAB`, `DRVSEL`, `RESFDC`, the NMI
handlers) and the DFS 0.90 ROM's init table at &8CE2:

| Use | Command bytes | Source |
|---|---|---|
| Specify timings, one of four drive profiles | `35 0D 02 08 C0` Tandon; `35 0D 03 08 C0` Shugart/Tandon; `35 0D 03 08 C7` Teac FD55 (1.20) or `35 0D 03 19 C4` (0.90); `35 0D 0C 0A C8` Olivetti | DOS03:411-416 |
| Bad tracks none, current track 0, both surfaces | `35 10 FF FF 00`, `35 18 FF FF 00` | DOS03:417-418 |
| Mode = &C1 (non-DMA) | `3A 17 C1` | DOS03:419 |
| Select drive and load head | `3A 23 48/88/68/A8` (drive 0/1, side 0/1) | DOS03:149-153, SELPAR |
| Confirm head loaded | `7D 23` read special register &23, test &08, re-send if not loaded | DOS03:156-162, RSROP |
| Restore | `69 00` (seek track 0 on drive 0) | DOS03:420 |
| Speed check | `5F 00 08 22` (verify track 0, sectors 8-9, 2 x 256) | DOS03:421 |
| Current sector | `3D 06` | DOS03:424 |
| Reset | write 1 then 0 to &FE82, about 3 us apart | DOS03:186-191 |
| Transfers | read &13, write &0B, drive status &2C, seek &29 | jsbeeb trace of `*CAT` under DFS 1.2: `3D`, `2C` x2, `29` x2, `13` |

So the DFS boot path needs: SPECIFY &0D/&10/&18, WRITE SPECIAL REGISTER &17 and
&23, READ SPECIAL REGISTER &06 and &23, READ DRIVE STATUS, SEEK, VERIFY, READ,
WRITE, per-byte NMIs, and the status bits &80/&20/&10/&08/&04 plus bits 1:0
reading zero. B-Em boots DFS with little more than that.

## 3. The five emulations in brief

### 3.1 beebjit (primary oracle)

- Keeps a 32-byte register file whose indices match the real chip's, and
  follows the ROM's control flow: parameters into R7 downwards, R23 as status and
  mode, R27 as the latched drive input, R28 as the index-pulse countdown, and
  multi-sector commands that re-dispatch the whole command per sector.
- Host side is instantaneous. The disc side runs once per FM byte (64 us, one
  32 x 2 us pulse word from the drive), decoding the 16 bit cells inside the
  callback; millisecond timers handle step, settle and head load.
- Deliberately reproduced quirks: bad-track integer overflow; a command written
  while busy is accepted without cancelling the running callbacks (The Sentinel
  depends on this); the write gate still writes outside a command (weak bits);
  bytes with missing clock bits are dropped mid-stream; an 11-byte GAP2 skip; a
  4-byte recovery after a non-matching header; two extra tracks tried on a track
  mismatch; FORMAT never times out (Disc Duplicator III).
- Unmodelled: real host-side latencies (27-211 us per command step, measured and
  recorded in the source but not applied); COMMAND FULL / PARAMETER FULL; the
  reset register held at 1; the disc-eating test mode; scan (fails &0A, as on a
  BBC); DMA mode (fails &0A); FORMAT with GAP5 != 0 and opcodes &24-&27/&30-&33
  (fatal bail).

### 3.2 jsbeeb

A function-by-function translation of beebjit; no behavioural divergence in the
controller for valid command streams. The only differences are pathological
(seek target over 255 does not wrap) or cosmetic (after a hard reset with a
pending result, &FE80 can show stale RESULT READY until the next status update;
`motorOn` glue tests the wrong drive). Its drive layer is also a translation, but
does not flush writes on spin-down, makes every loaded disc writeable, and has no
FSD. As an oracle it is a second opinion on Beebium's CPU and NMI plumbing, not
on the 8271.

### 3.3 MAME

An independent model with a bit-level FM engine over MAME's flux floppy layer
(4 us cells, 64 us per data byte, late DMA detected). Command phases are explicit
(IDLE, CMD, EXEC, RESULT). It records most SPECIFY values but acts only on step
rate and the unload count. Several deviations from the chip, listed in
section 4.

### 3.4 BeebEm

Sector arrays in RAM, no bitstream, no CRC, no index pulse, 80 us per byte, no
overrun detection. Seven commands are unimplemented and hang (a host dialog, no
completion): scan, write 128, write deleted, read 128, verify 128. Seek and
head-load delays come only from the drive-sound emulation, so they vanish with
sounds off. FSD support replays stored error codes and adds random bit
corruption, plus per-title hacks (Krakout, Mini Office II, 3D Pool, Disc
Duplicator 3).

### 3.5 B-Em

A thin controller over a generic media API shared with its 1770. All commands
decode with correct parameter counts. 136 us per byte on SSD (about half real
speed), no overrun detection, writes wait for the CPU. Scan, unknown commands and
most special registers complete with &18. Bad tracks and the mode register are
ignored. The deleted-data flag &20 is overwritten before the CPU sees it, and the
first Read ID entry usually produces no data. It boots DFS 0.90 and 1.20 anyway,
because the DFS path (section 2.6) avoids everything it omits.

## 4. Divergences

For each divergence, the "Real chip" column says what the hardware does and on
what evidence. "bj" = beebjit, "hw" = asserted by beebjit's hardware test ROM,
"wu" = the write-up, "ROM" = beebjit's reading of the chip's program ROM,
"DFS" = Acorn's DFS source. jsbeeb equals beebjit throughout and is omitted.

### 4.1 Host protocol and status

| # | Topic | beebjit | MAME | BeebEm | B-Em | Real chip |
|---|---|---|---|---|---|---|
| 1 | Status after command write | &90 (BUSY + RESULT READY) | CF (&40) during params, BUSY only after last param | &90 | &80 | &90; &80 after first parameter (hw) |
| 2 | COMMAND FULL / PARAM FULL | never reported | CF reported through the param phase; PF never | never | never | Not observable at 6502 speed; never reporting them is safe. DFS polls PF and BUSY so either is tolerated. |
| 3 | Command written while busy | accepted, callbacks not cleared | ignored | accepted, no check | ignored | Accepted, with side effects (wu); used by The Sentinel |
| 4 | Command written while a result is unread | accepted | ignored (RESULT phase) | accepted | accepted | Accepted (bj, consistent with 3) |
| 5 | READ DRIVE STATUS / READ SPECIAL REGISTER completion | RESULT READY, no INT | no RESULT READY, no INT, back to idle | RESULT READY, no INT | RESULT READY, no INT | RESULT READY, no INT (hw: status &10 after &2C) |
| 6 | WRITE SPECIAL REGISTER / SPECIFY completion | status &00, no INT | idle, no INT | status left at &10 | status &00 | &00, no INT (hw) |
| 7 | SEEK completion | INT + result | INT + result | INT + result | INT + result | INT + result per [DS, unverified]; beebjit's author was "not sure" on hardware. All agree; keep it. |
| 8 | Result register after read | value kept | value kept | reset to 0 | value kept | Kept (hw: reset leaves result intact) |
| 9 | Data register mirror &FE84-&FE87 | all four | all four | &FE84 only | &FE84 only | All four (hw: write &FE85, read &FE84) |
| 10 | Reads at &FE82 / &FE83 | byte counter MSB / LSB | &FF | 0 | 0 | Byte counter (bj, observed on a Model B) |
| 11 | Drive select from command | bits 7:6 at command start; both or none = no drive | bit 7 only; Read ID uses the track parameter (bug) | bits 6/7, both = drive 0 | bit 7 for every command byte | Bits 7:6 drive the SELECT outputs directly (ROM, wu) |

### 4.2 Interrupts and data transfer

| # | Topic | beebjit | MAME | BeebEm | B-Em | Real chip |
|---|---|---|---|---|---|---|
| 12 | Late servicing of a data byte | &0A after one byte time | &0A, but usually overwritten by &0E on reads | none | none (writes wait) | &0A (ROM) |
| 13 | Mode register at power-on | 0 (DMA): first transfer fails &0A until &17 is written | &C0: no per-byte INT until bit 0 set | ignored | ignored | 0; DFS writes &C1 at init so this is invisible to DFS (bj) |
| 14 | Mode bits on reset | cleared (share R23 with status) | reset to &C0 | ignored | ignored | Cleared (bj: R23 shared) |
| 15 | Scan | fails &0A (no DMA on BBC) | implemented, with a stale first byte and an inverted wildcard | unimplemented, hangs | fails &18 | Requires DMA, so fails on a BBC (wu, ROM) |

### 4.3 Drive interface

| # | Topic | beebjit | MAME | BeebEm | B-Em | Real chip |
|---|---|---|---|---|---|---|
| 16 | Drive status byte | base &81; TRK0 &02, RDY0 &04, WP &08, INDEX &10, RDY1 &40, all only while spinning | TRK0 at bit 0 and inverted, WP inverted, no fault | base &80, no index, TRK0 from logical track | base &80, physical TRK0, index simulated | beebjit's layout ("bit 7 and bit 0 appear to be set all the time" on hardware) |
| 17 | Ready | selected and spinning; latched low until READ DRIVE STATUS | head-load output, latched; in practice never not-ready on BBC | select flags | select latch, cleared on spin-down | Latched not-ready (hw: a SEEK from cold returns &10, a second READ DRIVE STATUS shows ready) |
| 18 | Index pulse | level, ~4 ms per 200 ms rev; always high with no disc | ~2 ms | none | simulated | ~4 ms on beebjit's author's Chinon drive; drive-dependent |
| 19 | No disc in drive | index always high, so no edges; not-ready if not spinning | no index, hangs BUSY forever | sector not found | sector not found | Drive-dependent; beebjit's "index held high" is what common drives do |
| 20 | Head load / motor | LOAD HEAD spins the selected drive | head load drives both motors and ready | sound emulation only | spin-up on every command | LOAD HEAD is the motor on the BBC (wu) |
| 21 | Head unload | after N index pulses (R15 high nibble; 0 now, F never); also deselects | after N index pulses | fixed 2 s | N revolutions x 0.2 s; 0 means never (bug) | N index pulses (ROM) |
| 22 | Dispatch clears write enable / head load | only when the select bits change | - | - | - | Always, per the write-up quoting the datasheet; beebjit differs. Open (section 5). |

### 4.4 Timing

| # | Topic | beebjit | MAME | BeebEm | B-Em | Real chip |
|---|---|---|---|---|---|---|
| 23 | Byte rate | 64 us (per drive pulse word) | 64 us (bit cells) | 80 us | 136 us | 64 us (FM, 125 kbit/s) |
| 24 | Step rate unit | 2 ms; 0 treated as 3 ms | 2 ms; 0 = no delay | unused | 1 ms | 2 ms, measured on hardware (bj); [DS, unverified] is ambiguous between 1 and 2 ms for 5.25" |
| 25 | Head settle | 2 ms units, only after a step with head already loaded | ignored | unused | 1 ms units | 2 ms units (bj, "all references") |
| 26 | Head load | 4 ms units; replaces settle when both apply | ignored | unused | computed, unused | 4 ms units (bj); BeebEm's comment says 8 ms. Open. |
| 27 | Host-side command latency | instant (measured values recorded: WSR 211 us, RDS 188 us, param 27-31 us) | instant | instant | instant | 27-211 us (bj measured). DFS reads status twice in its wait loop, suggesting BUSY can lag. Open. |
| 28 | Sector-not-found timeout | 3 index edges (2-3 revs), re-armed per sector, still running during transfer | 2 index edges | immediate | ~68 ms | 3 index pulses (ROM: R28 = 3) |

### 4.5 Seek and track registers

| # | Topic | beebjit | MAME | BeebEm | B-Em | Real chip |
|---|---|---|---|---|---|---|
| 29 | Bad-track remap | `if (bad1 <= t) t++; if (bad2 <= t) t++` on the target, 8-bit overflow | counter skips bad tracks while stepping: logical runs ahead of physical (wrong direction) | +1 per bad track, indexed by drive not surface | ignored | beebjit (ROM, wu: "integer overflow does occur") |
| 30 | Bad-track registers at power-on | 0 (so every seek is shifted +2 until DFS SPECIFYs &FF) | uninitialised | &FF | n/a | 0 (bj); DFS always SPECIFYs them |
| 31 | Current-track register | physical, updated to the target before stepping; both drives if single-actuator mode | logical counter | logical, no stepping | logical register, physical head separate | beebjit (ROM) |
| 32 | Seek to track 0 | always steps until TRK0 (up to 255) | recalibrate; &14 after 255 tries | sets register | true restore | Always steps to TRK0 (ROM, wu: logical 0 maps to physical 0). &14 is never produced (bj). |
| 33 | ID track mismatch | retry one then two tracks further on, then &18 | &18 at once | FSD searches +1 | ignored on SSD | beebjit (ROM) |
| 34 | Track 43+ | LOW HEAD CURRENT set | stored only | - | - | Set (ROM) |

### 4.6 Data, deleted data, errors

| # | Topic | beebjit | MAME | BeebEm | B-Em | Real chip |
|---|---|---|---|---|---|---|
| 35 | Deleted sector under READ DATA (&13) | read silently (no NMIs), CRC checked, continues to next sector, result ORed with &20 | transferred, no flag | transferred, &20 from FSD | flag lost | beebjit (ROM). [DS, unverified] may say the command terminates; open. |
| 36 | READ DATA AND DELETED (&17) | transfers, &20 | transfers, no flag | aliased to &13 | aliased to &13, flag lost | Transfers with &20 (ROM) |
| 37 | Bad data mark | &08 clock error | never &08 (reported as &0E) | never | never | &08 (ROM) |
| 38 | ID CRC error | ends the command at once, for any ID seen | read: any ID before a match; write: only the matching ID | never | &0C from HFE only | Ends at once (bj, observed) |
| 39 | Write protect | &12 from the latched WP bit after seek | never reported; write silently dropped | &12 | &12 | &12 |
| 40 | Read ID | 4 bytes per ID, count honoured | 6 bytes per ID, effectively one ID | ok | first ID's bytes usually lost | 4 bytes per ID (ROM, hw) |
| 41 | Bytes with missing clocks mid-stream | dropped | delivered | n/a | n/a | Dropped (bj, "from testing") |
| 42 | Data mark window after ID | 11-byte GAP2 skip, then sync anywhere until timeout | mark must appear in bytes 11-23 | n/a | n/a | beebjit (ROM: 11-byte skip; practical minimum 12) |

### 4.7 Format

| # | Topic | beebjit | MAME | BeebEm | B-Em | Real chip |
|---|---|---|---|---|---|---|
| 43 | Layout | GAP1 x FF + 6 x 00, ID (CPU-supplied C/H/R/N), CRC + FF, GAP2 fixed 11 x FF + 6 x 00, data mark, E5 fill, CRC + FF, GAP3 x FF + 6 x 00, GAP4 to index | similar, CPU-supplied IDs; GAP5 index mark supported; null pointer at start | 10 x 256 only, IDs discarded | geometry from image, IDs discarded | beebjit (ROM, hw format/read-ID/write/read test) |
| 44 | GAP5 != 0 (index mark) | fatal bail | writes index mark | ignored | ignored | Unknown in beebjit; MAME's reading is plausible. Rare. |
| 45 | Timeout | none | ends at next index | n/a | n/a | None (bj; Disc Duplicator III needs it) |

### 4.8 Reset

| # | Topic | beebjit | MAME | BeebEm | B-Em | Real chip |
|---|---|---|---|---|---|---|
| 46 | Reset register | acts on writing 1, ignores 0 | acts on exactly 1 | any write | bit 0 set | Level-sensitive: held at 1 the chip ignores commands (bj comment). Nobody models the hold. |
| 47 | State after reset | status cleared (incl. mode), drive output 0, result and data kept, RESULT READY kept | mostly cleared, pcn kept | cleared, bad tracks &FF | status 0, latch and NMI kept | beebjit ("on a real machine, status appears to be cleared but result and data register not") |
| 48 | BREAK | resets the 8271 | resets | resets | resets | All tie it to system reset |
| 49 | Disc-eating test mode | not modelled | not modelled | not modelled | not modelled | Real, trigger unknown (in silicon, per the write-up); not to be modelled |

## 5. Open questions

The evidence does not settle these. None is on the DFS boot path; they matter
only to disc-level software and copy protection.

1. **Host-side latency (27-211 us).** beebjit measured it and chose not to
   model it. DFS's double status read hints that BUSY may not be visible on
   the very next access. Start instant, as beebjit does, and add latency only if
   a title needs it.
2. **Deleted data under READ DATA (&13).** beebjit's ROM-derived "read silently
   and continue" against a possible datasheet "terminate". Follow beebjit.
3. **Dispatch and head load.** The write-up says dispatch clears write enable
   and head load "per the datasheet"; beebjit does it only on a drive change.
   Clearing head load on every dispatch would force a head-load delay on every
   command, which beebjit's timing tests would presumably have shown. Follow
   beebjit.
4. **Head-load unit** (4 ms beebjit, 8 ms BeebEm comment). Follow beebjit, which
   was compared with hardware.
5. **Both drives selected.** beebjit selects no drive. Unknown.
6. **Does LOAD HEAD spin both drives?** MAME drives both motors; beebjit only the
   selected drive. On a shared drive cable MOTOR ON is common to both drives,
   which favours MAME. It affects only the activity indicators, since only the
   selected drive is read.
7. **SEEK completion interrupt.** Everyone raises it; beebjit's author was not
   sure he saw it on hardware. Keep it (DFS waits on BUSY, not the NMI, for
   seeks).

## 6. Local resources

### 6.1 DFS ROMs

ROMs are copyright and none were copied or committed.

| ROM | Where | MD5 | Contents |
|---|---|---|---|
| Acorn DFS 0.90 (8 KB) | jsbeeb `public/roms/b/DFS-0.9.rom`; b-em `roms/general/dfs09.rom`; beebjit `roms/DFS-0.9.rom`; MAME romset `bbc_acorn8271.zip::dfs090.rom` | 803530149c274994fabc6d28cbe03679 | DFS 0.90 |
| Acorn DFS 0.98 (8 KB) | MAME `bbc_acorn8271.zip::dfs098.rom` | 5a3ddfae96178e025f27bf1f69bfa629 | DFS 0.98 |
| DNFS "1.20" (16 KB) | jsbeeb `b/DFS-1.2.rom`; beebjit `roms/acorn_dnfs.rom`; BeebEm `DNFS.rom`; MAME `dnfs120.rom` | 5daed103918277e2065dd7e8d23e57a5 | NFS 3.60 + DFS 1.20 |
| DNFS 1.00 (16 KB) | jsbeeb `us/USDNFS.rom`; b-em `usdnfs.rom`; MAME `dnfs100.rom` | d636ffb7a80fc29b133d32e4250fbab6 | NFS 3.40 + DFS 1.00 |
| Beebium `roms/acorn-dnfs_3_02.rom` | in the repository | ab3fcb1f9d6c7599f79bc31fa5909097 | **NFS 3.62 + DFS 1.22** |
| Watford DFS 1.30-1.44 | MAME `bbc_acorn8271.zip` | - | 8271 Watford DFS |

Notes:

- "DNFS 1.20" (named by DFS version, MAME and jsbeeb) and Acorn's "DNFS 3.00"
  source release (DFS 1.20 + NFS 3.60) are the same components. Beebium's
  "DNFS 3.02" is a later revision with DFS 1.22; the name is a package label.
  `docs/networking.md` and `docs/level-3-file-server-setup.md` call its DFS
  "1.20" and should say 1.22.
- No standalone 8 KB DFS 1.20 exists locally; every "DFS 1.2" is the 16 KB DNFS
  image. No DNFS 3.34 exists locally.
- Because `acorn-dnfs_3_02.rom` is already in the repository, an 8271 boot test
  with DFS 1.22 can run in CI. DFS 0.90 / 1.20 tests must be opt-in and skip
  when the ROM is absent.

### 6.2 Oracles

- **MAME is ready and is the independent oracle.** The harness is on master
  (`oracle/mame/`, commit fd7ab0ba; the `oracle-mame-harness` branch no longer
  exists). MAME 0.289 is installed and `~/.local/share/beebium/mame-roms`
  verifies as good for `bbcb`, including all nine 8271 BIOS options
  (`-fdc acorn8271,bios=dfs090|dfs098|dnfs100|dnfs120|wdfs1xx`). A `*CAT` of
  `tests/assets/discs/Disc016-Boffin.ssd` ran in about 3 s. Limits: fixed waits
  in emulated seconds, no 8271 internal state, and MAME writes `cfg/` into its
  working directory, so run it from a scratch directory. Its own 8271 deviations
  (section 4) mean it settles screen-level outcomes, not register-level detail.
- **jsbeeb works headlessly** (`B-DFS1.2` and `B-DFS0.9` models) and its `*CAT`
  screen was identical to MAME's. The `oracle/` lockstep `DiffRunner` cannot
  survive disc I/O and does not compare FDC state, so use end-state comparison
  (wait for input, then compare screen and memory). jsbeeb's 8271 is beebjit's,
  so agreement with jsbeeb checks the port, not the chip. Its private registers
  (`fdc._regs`, `_status`, `_driveOut`) can be read without side effects, which
  makes it the best register-level reference for the port.

## 7. Corrections to existing Beebium documentation

`docs/disc-subsystem.md`, "8271 vs WD1770 Motor Control" and "Implications for
8271 Implementation", says the 8271 controls the motor through bit 4 of a latch
at &FE80. That is wrong for an 8271 board: there is no latch; &FE80-&FE83 are the
8271's own registers, and the motor is the 8271's LOAD HEAD output (special
register &23 bit 3). Those sections should be rewritten when the controller
lands. The "8271 vs 1770 Detection" section is right, and should add that DFS
1.20 tests bits 1:0 of &FE80, not the whole byte.

## 8. Proposed design

### 8.1 Basis: port beebjit, structured for Beebium

Port beebjit's model rather than MAME's or a fresh datasheet model:

- It is the only model built from the chip's program ROM and checked against
  hardware, and the only one that reproduces what copy protection depends on
  (command while busy, track-mismatch retry, latched ready, clock-error
  framing, weak bits from the write gate).
- Beebium's drive layer is already beebjit-shaped: `DiscDrive` serves one
  32 x 2 us pulse word per 64 us FM byte, which is exactly beebjit's
  `intel_fdc_pulses_callback` granularity. No change of granularity is needed.
- jsbeeb proves the logic ports cleanly to another language, and gives a
  register-inspectable reference for differential tests.

**Licence (decision needed).** beebjit is "version 3 of the GNU GPL" (GPL-3.0
only); jsbeeb's translation is GPL-3.0-or-later; Beebium is GPL-3.0-or-later. A
translation of beebjit's file is a derivative of GPL-3.0-only code, so that file
would be GPL-3.0-only. The combined work remains distributable under GPL-3.0, but
loses "or later" for that file. Alternatives: port from jsbeeb (whose own
derivation from beebjit raises the same question), ask Chris Evans for
GPL-3.0-or-later on `intel_fdc.c`, or write the model fresh from this study's
facts (behaviour, register roles and constants are facts; beebjit's code is
expression). Recommendation: write it fresh, structured the Beebium way and
test-first against the behaviour tabulated here, citing beebjit and the write-up
as references, as `WD1770.hpp` does for B2. Asking Chris Evans is a cheap
parallel step.

### 8.2 Components

Mirror the WD1770 split between the chip and the board:

- **`Intel8271`** (`src/core/include/beebium/disc/Intel8271.hpp`, with a `.cpp`
  if it grows past header size). The chip: R0-R31, external status / result /
  data, the parameter rule, the command dispatcher, the seek / head-load /
  settle state machine with a millisecond timer, the bit-level FM read path and
  byte-level write path, the index-pulse countdown, the drive control output
  and input ports, and power-on vs. reset semantics. Drive attachment via
  `DiscDrive*` like `WD1770`.
- **`Intel8271DiscController : DiscControllerInterface`** (the Acorn 8271 board,
  the name the docs already anticipate). Decode `offset & 7`: 0-3 to the chip
  with CS, 4-7 to the data register (DACK, A0/A1 ignored). Board wiring: SELECT0
  and SELECT1 select the drive, SIDE goes to both drives' side select, LOAD HEAD
  drives `DiscDrive::set_motor()` (for the selected drive only, per beebjit;
  section 5 item 6), NMI = INT. `fdc_chip()` returns "8271".
- **Registry.** `DiscControllerRegistry` gains `acorn-8271` ("Acorn 8271",
  chip "8271", "Original Intel 8271 disc interface for the BBC Model B").
  `--fdc acorn-8271`, `list-fdcs`, presets and `DiscService` pick it up from
  the registry with no further plumbing.

### 8.3 What the drive layer must add

| Need | Today | Proposal |
|---|---|---|
| Rotation while idle (index edges count for head unload; drive status INDEX bit) | the WD1770 advances the head only while a command runs | the 8271 controller advances the selected spinning drive every 64 ticks itself, as the WD1770 does inside commands; `DiscDrive` stays passive |
| Index as a level about 4 ms wide | `is_index_pulse()` is true only at position 0 | add `DiscDrive::index_level()`: `head_position < track_length * 4 / 200` with a disc, always true without one (beebjit `disc_drive.c`) |
| Write protect reads 0 with no disc | already so | none |
| Write fail-safe when protected | `write_pulses` already drops writes | none |
| Weak bits from unformatted (all-zero) regions | quasi-random pulses only with no disc | defer; needed only for weak-bit protection (later FSD/HFE work) |
| Write-back flush on write inactivity | private to `WD1770` | **refactor first**: extract the flush-on-idle policy (`FLUSH_IDLE_TICKS`, `flush_dirty_drives`) into a small shared helper both controllers own |
| 40-track discs in an 80-track drive (double step) | none | out of scope; note for later |

### 8.4 Clocking

The socket's `tick()` is 1 MHz, which suits the 8271 exactly: one FM byte is
64 ticks, one millisecond is 1000 ticks (beebjit uses 128 and 2000 ticks at
2 MHz). The controller keeps a byte-phase counter and does one pulse-word step
per 64 ticks while the selected drive spins, and counts down its millisecond
timer otherwise. All delays are in emulated cycles; nothing is wall-clock.
Host register accesses are instantaneous, as in beebjit (section 5 item 1).

`DiscDrive` fixes a pulse word at 64 us regardless of track length, where
beebjit stretches byte time to keep 200 ms per revolution for non-standard
track lengths. Keep Beebium's fixed rate, as the WD1770 does; revisit with HFE
tracks of unusual length.

### 8.5 NMI and Machine.hpp

No change to `Machine.hpp`. `nmi_pending()` returns the INT bit. The existing
ordering (sample NMI, then tick, on even cycles) gives the clean edges the 8271
needs, because INT drops on the data or result access and rises again on the
next byte callback 64 us later. beebjit's abort path (late DMA, &0A) drops INT
before raising the completion INT; on Beebium that drop must persist for at
least one 1 MHz sample so the completion NMI is a new edge. A unit test pins
this (section 9).

### 8.6 Reset

`DiscControllerInterface::reset()` serves both power-on and BREAK
(`ModelBHardware::reset()` and `soft_reset()` both call `disc_socket.reset()`),
but the 8271 needs two behaviours: power-on clears R0-R31 (bad tracks 0,
mode 0), while BREAK clears status and the drive output but keeps result, data
and the register file. Proposal: make the distinction explicit in the interface
(`reset(ResetKind::PowerOn | ResetKind::Break)`, or a separate `power_on()`),
with the WD1770 board treating both alike. It is a small interface change
touching the five socketed hardware policies and the tests that call `reset()`.

### 8.7 Which machines accept it

All five socketed variants get it automatically from the registry: Model B,
ROM/RAM board, ATPL Sidewise, Watford ROM/RAM and Integra-B. The B+ has a
built-in WD1770 with no socket; an 8271 option there was never manufactured, so
leave it out of scope (issue #87 says optional).

### 8.8 ROM pairing

The failure mode is silent: DNFS on a WD1770 deadlocks or does nothing, and
DFS 2.26 on an 8271 finds no controller. Proposal, minimal and server-side:

- A **DFS ROM manifest** in the core: a table of known filing-system ROMs keyed
  by SHA-1 (the hashes in section 6.1 plus DFS 2.26 and the other 1770 DFSs),
  each with the FDC chip it requires ("8271", "WD1770") and a display name.
  Unknown ROMs are not judged.
- `resolve-preset` and launch check every fitted sideways ROM against the
  socket's controller and report a **warning** at the ROM's preset path, such as
  "DFS 1.20 needs an 8271 disc controller; this machine has a WD1770". The
  macOS New Machine dialog already surfaces resolve-preset diagnostics against
  rows, so it needs no new knowledge (server stays the single source of truth).
- Warning rather than error: a deliberate mismatch is a legitimate
  configuration (a DNFS fitted for its NFS on a 1770 machine, as some presets do
  today).
- Fix the existing fixtures that pair DNFS with the 1770
  (`clients/beebium-python-client/tests/tube_test_helpers.py` notes the trap).

### 8.9 Presets and UI

One new bundled preset, "Model B with 8271 DFS", using `acorn-8271` and the
bundled `acorn-dnfs_3_02.rom`. The macOS storage panel talks to `DiscService`
and should need nothing; `fdc_chip` already flows through as "8271".

## 9. Test plan

Test-first throughout; targeted tests per commit, full suites left to CI.

1. **Host protocol unit tests** (`tests/test_intel8271.cpp`), port of the
   hardware-verified sequences in beebjit's `make_8271_rom.c`:
   - power-on status &00, result &00, data &00; data written at &FE85 reads back
     at &FE84;
   - READ DRIVE STATUS with no drive: status &10, result &81, status &00 after
     the result read;
   - WRITE SPECIAL REGISTER: &90 after the command, &80 after the first
     parameter, &00 at completion;
   - READ SPECIAL REGISTER R7 returns &07;
   - SEEK 0 from cold: status &18, result &10, exactly one NMI; READ DRIVE
     STATUS then shows TRK0, and a second shows ready (latched not-ready);
   - bits 1:0 of status always 0; &FE82/&FE83 reads give the byte counter.
2. **DFS init sequence**: the DFS 1.20 `CMDTAB` and `DRVSEL` sequences from
   section 2.6 complete with the expected status after each step, and the
   head-loaded check via READ SPECIAL REGISTER &23 succeeds.
3. **Per-command unit tests** against tracks built with `TrackBuilder` and
   checked with `TrackDecoder`:
   - seek: step count and direction, step time 2 ms x rate (0 means 3 ms),
     settle and head-load delays, bad-track remap including overflow, seek to 0
     stepping until TRK0, single-actuator mode;
   - read &13: one NMI per byte at 64 us spacing, status &8C per byte and &18 at
     completion, multi-sector, late DMA &0A when a byte is not taken, and the
     INT edge across the abort (section 8.5);
   - deleted data under &13 (silent, &20) and &17 (transferred, &20); verify;
   - Read ID: 4 bytes per ID, count honoured, index wait;
   - write &0B / write deleted &0F, then decode the track;
   - format: gap layout, CPU-supplied IDs, E5 fill, no timeout;
   - errors: &08 bad data mark, &0C ID CRC (ends at once), &0E data CRC, &10
     latched not-ready, &12 write protect, &18 after three index pulses and
     after the track-mismatch retries;
   - head unload after N index pulses, never for F;
   - reset semantics (power-on vs BREAK); command written while busy is
     accepted.
4. **Board and integration** (`tests/test_disc_controller_socket.cpp` and a new
   integration file): &FE80-&FE9F decode and mirroring, NMI through `Machine`,
   registry and `--fdc acorn-8271`, the motor and side wiring.
5. **Image round trips**: write through the 8271, flush, reload, compare, for
   SSD, DSD and HFE.
6. **DFS boot** (C++ or Python scenario, CI-safe with the bundled ROM): a Model B
   with `acorn-8271` and DNFS 3.02 (DFS 1.22) boots and `*CAT`s
   `Disc016-Boffin.ssd`; `SAVE` then `LOAD` round trip; opt-in variants with
   DFS 0.90 and DFS 1.20 that skip when the ROM is absent.
7. **Pairing diagnostics**: resolve-preset warns for DNFS on `acorn-1770` and
   DFS 2.26 on `acorn-8271`; no warning for matched pairs or unknown ROMs.
8. **Differential, opt-in**: the same boot-and-`*CAT` scenario through MAME
   (independent) and jsbeeb (beebjit lineage), comparing screen memory;
   register-level comparison against jsbeeb's `fdc._regs` for selected command
   traces.
9. **Acceptance (#85, #86)**: Flip! (`Disc013-FlipTheCartoonStrategyGame.ssd`,
   SHA-256 064ef866...dad586, downloaded locally, never committed) boots under
   `acorn-8271` with DFS 1.2x, shows clean spaces on the title screen, and
   reaches its options menu. Opt-in, skips without the image. Note that what
   fixes Flip! is DFS 1.2x's workspace layout (FLIP2 loads at &1100, inside
   DFS 2.26's workspace); the 8271 is what makes DFS 1.2x usable.

## 10. Decisions for the architect

1. Licence route for the model (section 8.1): fresh implementation from this
   study (recommended), a beebjit port as GPL-3.0-only, or ask Chris Evans.
2. The `reset()` interface change (section 8.6).
3. Extracting the write-back flush policy from `WD1770` first (section 8.3).
4. The ROM manifest: warning-only and keyed by SHA-1 (section 8.8).
5. Whether LOAD HEAD lights both drives' activity indicators (section 5 item 6).
6. B+ out of scope.
