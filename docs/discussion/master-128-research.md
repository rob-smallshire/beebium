# The BBC Master 128: research for a Beebium variant

Issue #189. This document records what a Master 128 is, as against the Model B and B+ that
Beebium already builds, how four emulators approach it, and what Beebium would have to add and
change. It is the basis for deciding what to build and in what order. The first deliverable is
the basic Master 128 configuration: MOS 3.20, 128K RAM, the on-board 1770, the real-time clock
and CMOS RAM, the keyboard with its keypad, and the external Tube and 1 MHz bus. The Master
Turbo, Master 512, Master ET and Master Compact are noted where they bear on the design but
are not in scope.

Sources: the New Advanced User Guide transcript in `docs/manuals_text/` (thin on the Master's
internals; it never names ANDY or LYNNE, does not describe the internal connectors, and prints
the ITU bit the wrong way round); the B2, B-Em, BeebEm and beebjit sources on this machine;
Acorn's Master Reference Manual Part 1, Service Manual, Hardware Specification and the Watford
Advanced Reference Manual, the MOS 3.20 source, and the stardot threads that measured real
machines (section 6 lists them). Where the sources disagree or are silent, section 5 names the
measurement to make on a real Master.

## 1. What the Master 128 is, against the B and B+

| Area | Model B | Model B+ | Master 128 |
|---|---|---|---|
| CPU | 6502A (NMOS), 2 MHz, clock stretched for 1 MHz devices | 6512A (NMOS), 2 MHz | 65SC12 (CMOS, no Rockwell bit instructions), a constant 2 MHz with RDY wait states for 1 MHz devices |
| Main RAM | 32K | 32K + 20K shadow + 12K private (ANDY, &8000-&AFFF) | 32K + 20K shadow (LYNNE, &3000-&7FFF) + 8K filing-system RAM (HAZEL, &C000-&DFFF) + 4K private (ANDY, &8000-&8FFF) + 64K sideways RAM (banks 4-7) |
| MOS | MOS 1.20, 16K | MOS 2.00, 16K | MOS 3.20 in a 128K ROM: 16K MOS plus sideways banks 9-15 (DFS 2.24, ViewSheet, Edit, BASIC IV, ADFS 1.50, View, Terminal) |
| Paging registers | ROMSEL &FE30 (write only) | ROMSEL &FE30 with bit 7 = ANDY; ACCCON &FE34 bit 7 = VDUSEL | ROMSEL &FE30 bits 0-3 + bit 7 = ANDY, readable; ACCCON &FE34 bits IRR TST IFJ ITU Y X E D, readable |
| Disc controller | 8271 or 1770 on a daughter board at &FE80/&FE84 | 1770 at &FE80 (latch) / &FE84 | 1770 on the motherboard: latch &FE24, registers &FE28-&FE2B |
| Analogue converter | uPD7002 at &FEC0 | &FEC0 | uPD7002 at &FE18 |
| Econet | ADLC &FEA0, station ID register at &FE18 | same | ADLC &FEA0 on a plug-in module; station number in CMOS byte 0; INTOFF &FE38, INTON &FE3C |
| Clock and non-volatile RAM | none | none | MC146818 on the System VIA (port A data, PB6 chip enable, PB7 address strobe, latch bit 1 R/W, bit 2 DS), 50 bytes of CMOS RAM, battery backed |
| Keyboard | 10 columns, start-up links in row 0 | same | 13 columns: 74 keys + 19-key keypad; no links, boot options from CMOS |
| Speech | socket for TMS5220 | same | none; the clock sits in its place |
| Tube | external socket at &FEE0 | same | external socket plus an internal connector; ACCCON ITU selects which |
| 1 MHz bus | external connector, FRED/JIM | same | external connector plus two cartridge slots (ROMs 0-3) and an internal connector; ACCCON IFJ routes FRED/JIM to the cartridges at 2 MHz |
| ROM sockets | 4 | 4 | IC27 (ROM 8), IC37 (6/7) and IC41 (4/5) accepting 32K ROMs in place of sideways RAM via links LK18/LK19, and two cartridge slots (0/1 and 2/3) |
| Video, sound, VIAs, serial | | | unchanged parts; Video ULA decodes &FE20-&FE23 only; no Mode 7 alias at &3C00 |
| Machine identity | OSBYTE 0 = 1 | 2 | 3; INKEY-256 = &FD |

### 1.1 Memory and paging

The Master has 128K of RAM: 32K main, LYNNE (20K), HAZEL (8K), ANDY (4K) and four 16K sideways
banks. The paging state is two readable registers.

ROMSEL (&FE30-&FE33): bits 0-3 select the sideways bank at &8000-&BFFF; bit 7 maps ANDY over
&8000-&8FFF, leaving &9000-&BFFF on the selected bank. Bits 4-6 are kept on read by some
emulators; the hardware latches only the bits it decodes (to verify, section 5).

ACCCON (&FE34-&FE37):

| Bit | Name | 0 | 1 |
|---|---|---|---|
| 7 | IRR | | a software-set interrupt: an open-drain output on the I/O controller pulls nIRQ low while the bit is set (level, not edge); nothing to do with the Tube, and MOS 3.20 never sets it |
| 6 | TST | normal | hardware test; Acorn: "must be zero at all times". The emulators that model it make reads of &FC00-&FFFF return the MOS ROM; unverified on hardware |
| 5 | IFJ | &FC00-&FDFF is the external 1 MHz bus | &FC00-&FDFF is the cartridge bus, at 2 MHz (500 ns) |
| 4 | ITU | the external Tube | the internal Tube (the New AUG has this reversed; the Reference Manual and MOS source agree on this polarity) |
| 3 | Y | MOS VDU code at &C000-&DFFF | HAZEL, 8K RAM at &C000-&DFFF |
| 2 | X | main RAM at &3000-&7FFF | LYNNE at &3000-&7FFF |
| 1 | E | the VDU driver uses main RAM | the VDU driver uses LYNNE |
| 0 | D | the CRTC displays main RAM | the CRTC displays LYNNE |

The shadow access rule, in Acorn's own terms (Reference Manual F.2 and the Advanced Reference
Manual's flowchart): X set sends every access to &3000-&7FFF to LYNNE "irrespective of the
opcode address". With X clear and E set, the hardware decides per bus cycle from the CPU's
SYNC pin: if the previous cycle was an opcode fetch whose address lay in &C000-&DFFF, and this
cycle is not itself an opcode fetch, the access goes to LYNNE; otherwise to main RAM. So
opcode fetches are never redirected, operand fetches and data accesses of an instruction whose
opcode lies in &C000-&DFFF are, and the whole of &C000-&DFFF counts, nothing in &8000-&BFFF or
&E000-&FFFF. Acorn says nothing about Y's part. Emulators differ: B2 and beebjit treat MOS code
as seeing shadow when (Y and X) or (not Y and E), so Y set suppresses E (jsbeeb too); B-Em,
BeebEm and MAME ignore Y. It matters only for code executing out of HAZEL, which the MOS never
does; it is a hardware question (section 5). The emulators also differ in where they test the
opcode address: B2 by the 4K page of the opcode, BeebEm by the opcode address per access,
beebjit by the post-instruction PC (off at the edges). Beebium's B+ `HasOpcodeFetchLatch`
already latches a flag at each opcode fetch, which is the SYNC rule exactly; the Master policy
needs its own window and the X override.

The surprising corner is X = 1, E = 0: user code sees LYNNE and the VDU driver's own writes
also land in LYNNE, since X wins; the game Planet Nubium's original `*FX108` screen clear
behaved differently on two real Masters from every emulator of the time, and is a test case.

The display reads LYNNE when D is set, switching at the character where the write lands (measured
on two Masters). With D set the CRTC addresses the second 64K bank throughout, so screen
addresses below &3000 display ANDY and HAZEL, not main RAM; beebjit models this against a
hardware-verified test disc.

The B+'s rule is different (its VDU-code window also covers &A000-&AFFF while ANDY is paged
in), and the Integra-B's different again, so the Master gets its own policy.

The 128K MOS ROM: the first 16K is the MOS at &C000-&FFFF, the other seven 16K parts are banks
9 to 15. Both MOS 3.20 and MOS 3.50 are in `roms/`, split into their eight banks and named by
the convention (`docs/deployment.md` tabulates them with bank numbers and versions). The Terminal
ROM in bank 15 carries MOS overflow code and strings and so exists in a per-MOS form; the other
six sideways ROMs differ in version between the two releases. The 3.20 MOS half is the mdfs.net
image with Acorn's credits in the bytes under the I/O region, where B-Em's copy has &FF. Each
MOS is to be a distinct preset. Beebium has no manifest beyond DFS ROMs.

### 1.2 SHEILA differences

| Address | Master 128 | Note |
|---|---|---|
| &FE18-&FE1F | uPD7002 ADC | moved from &FEC0; &FEC0 is unmapped |
| &FE20-&FE23 | Video ULA | decodes 4 bytes, not 16 |
| &FE24-&FE27 | 1770 drive control latch | write only; a read returns the stale bus |
| &FE28-&FE2B | 1770 registers | &FE2C-&FE2F: B-Em says undecoded, BeebEm mirrors; verify |
| &FE30-&FE33 | ROMSEL, readable | |
| &FE34-&FE37 | ACCCON, readable | |
| &FE38 / &FE3C | Econet INTOFF / INTON | were &FE18 / &FE20 on the B |
| &FE80-&FE9F | nMODEM select: the internal modem connector PL12 | the B's 8271 / 1770 daughter-board space; measured on hardware to decode &FE80-&FE9F |
| &FEA0-&FEBF | 68B54 ADLC on the plug-in Econet module (SK5/SK6) | 2 MHz |
| &FEC0-&FEDF | unmapped | MMFS and others use &FEDC |

Slow devices: the Master does not stretch the CPU clock as the B does. Phi2 runs at a constant
2 MHz and the I/O controller holds RDY for the wait states that align a 1 MHz access, so the
cycle cost is the B's but peripherals clocked from Phi2 see a steady 2 MHz. The Reference
Manual's SHEILA table marks as 1 MHz: the CRTC, ACIA, serial ULA, ADC (&FE18), Video ULA
(&FE20; but measured at 2 MHz on two real Masters, so the table is wrong there), the 1770
registers (&FE28; MAME and others treat them as 2 MHz, unmeasured), both VIAs and the modem
select (&FE80); and as 2 MHz: the 1770 control latch (&FE24), ROMSEL, ACCCON, INTOFF, INTON,
the ADLC and the Tube. beebjit and B2 stretch &FE00-&FE1F, &FE28-&FE2B and &FE40-&FE9F. The
Model B stretches &FE00-&FE1F, &FE40-&FE7F and &FEC0-&FEDF. FRED and JIM are 1 MHz on the
external bus and 2 MHz when IFJ routes them to the cartridges. The Master also takes one cycle
longer than the B to enter an IRQ handler (stardot measurement).

Reads of unmapped addresses return the last byte on the data bus, which is the video fetch:
&20 in Mode 7, &00 in Mode 0 on a real Master, not the &FE that B-Em, BeebEm and jsbeeb
return. ANFS 4.25 probes &FEA0 for an ADLC and misbehaves with &20 on the bus, so this is
observable. B2 and beebjit (as a TODO) model it. The 1770 latch reads the same way.

### 1.3 The 1770 and its latch

The latch at &FE24: bit 0 DS0, bit 1 DS1, bit 2 RES (active low; pulse low at least 50 µs),
bit 3 DS2 (a third drive; the manual says up to three), bit 4 SEL (side), bit 5 DDEN (1 = single
density). The B+ latch at &FE80 has drive in bits 0-1, side in bit 2, density in bit 3 and reset
in bit 5, so Beebium's `Acorn1770DiscController` needs a latch-layout variant, and the latch
sits inside the Video ULA's old &FE20-&FE2F range. DFS 2.24 and ADFS 1.50 live in the MOS ROM;
no 8271 exists.

### 1.4 The real-time clock and CMOS RAM

An MC146818 hangs off the System VIA, in the speech chip's position: port A is its data bus,
shared with the keyboard and sound; PB6 is chip enable and PB7 the address strobe, driven
directly by port B; addressable-latch bits 1 and 2 (the B's speech read and write) are R/W and
DS. The chip enable passes through a transistor fed from the battery so that it drops when
mains power fails, and Acorn warns that DS before CE gives the chip a phantom access. The MOS's
sequence (MOS 3.20 source, with interrupts disabled around it): with CE and DS low, set AS, put
the register number on port A, raise CE, drop AS (the address latches on the falling edge);
then for a read set R/W, make port A input, raise DS and read; for a write clear R/W, raise DS
and write; then drop DS and CE. B-Em latches the address on any port B write with bit 7 set
(level), B2 and BeebEm on the falling edge; the datasheet (`docs/datasheets/MC146818.pdf`,
Motorola 1988, reprinted by Freescale) says the falling edge of AS latches AD0-AD5, which
Beebium's `Mc146818Rtc` already models through `write_address`. On every reset,
soft or hard, MOS 3.20 writes register A = &20 (oscillator on, periodic interrupt off) and
register B = &02 (BCD, 24-hour, no interrupts), and reads the time only for OSWORD &0E and
`*TIME`; its own TIME counter is VIA-driven. CMOS byte 0, the station number, is write-protected
in the OSBYTE &A2 path.

Registers 0-9 are time and alarm, 10-13 are A to D, 14-63 the 50 bytes of CMOS RAM that OSBYTE
&A1/&A2 address as 0-49. The byte map (manual, p.357): 0 Econet station; 1-2 file server;
3-4 printer server; 5 filing system (low nibble) and language (high) ROM numbers; 6-7 ROM
insert bits; 8 Edit; 9 telecoms; 10 mode, shadow, interlace, TV offset; 11 drive, caps, ADFS
options; 12-13 auto-repeat delay and rate; 14 printer ignore character; 15 Tube on, baud,
printer; 16 loud, Tube internal/external, no-scroll, boot, serial format; 17 ANFS; 18 Compact
joystick; 19-29 reserved; 30-45 one byte per ROM 0-15; 46-49 user. The clock's IRQ reaches the
CPU only through link LK4, unmade as shipped, so the alarm and periodic interrupts are not
observable on a stock machine; the update-in-progress bit is.

What else the datasheet fixes, and the Master's wiring makes observable. The latch takes six
address bits, so register numbers above 63 wrap. CE is not latched: it must be low from before
AS falls until DS ends, which is why Acorn's phantom-access warning exists and why the MOS raises
CE before dropping AS; PB6 therefore gates the strobes rather than the data. The level of DS at
the falling edge of AS selects the bus protocol (MOTEL): the MOS holds DS low, so the chip runs
in Motorola mode, drives the bus while DS is high on a read and latches the data on the falling
edge of DS on a write. Reading register C returns the flags and clears them all, IRQF included;
registers C and D, UIP in A and bit 7 of the seconds byte are read-only. The RESET pin clears
PIE, AIE, UIE and SQWE and the four flags and nothing else (the time, the RAM, SET, DM, 24/12,
DSE and the divider and rate bits survive); on the Master the pin is on the system reset, so
every Break does this, which the MOS's A = &20, B = &02 rewrite then hides. VRT in register D
is set by reading D and cleared only by the power-sense pin going low. The time base is a
32.768 kHz crystal (X1 on sheet 2), so the update cycle is 1984 us with UIP set 244 us before it;
the ten time and alarm bytes read undefined during the cycle. An alarm byte of &C0-&FF is a
"don't care". Daylight saving (DSE) follows the US rule of the day, the last Sundays of April
and October. The existing `Mc146818Rtc` implements the six-bit latch, the clear-on-read of C,
the read-only bits, the RESET pin as `reset()`, the 1984/244 us UIP window and the alarm codes;
it stores DSE without acting on it and reports VRT always set.

Every emulator surveyed simplifies the clock to host time plus an offset with no UIP, alarm or
periodic interrupt. Beebium's `Mc146818Rtc` (from the Integra-B) has `tick`, `irq_pending`,
`use_emulated_clock` and a battery image, so the Master reuses the device whole and adds only
the System VIA wiring, a Master CMOS seed, and a `BatteryBackedUnit` for `nvram/`.

The `*CONFIGURE` options map onto the bytes as beebwiki tabulates (section 6): byte 5 FILE
and LANG, 10 MODE and TV, 11 FDRIVE, CAPS/NOCAPS/SHCAPS, DIR/NODIR, FLOPPY/HARD, 12 DELAY,
13 REPEAT, 14 IGNORE, 15 TUBE/NOTUBE, BAUD, PRINT, 16 LOUD/QUIET, INTUBE/EXTUBE, SCROLL/NOSCROLL,
BOOT/NOBOOT, DATA, 17 the ANFS options, 19 COUNTRY, 30-45 one byte per ROM.

MOS 3.20 has exactly one power-on key test: R held through power-on zeroes CMOS bytes 1-49,
sets bytes 6 and 7 to &FF (every ROM inserted), prints "CMOS RAM reset / Press break to
continue" and waits. Its "defaults" are therefore all zero: LANG 0, FILE 0, MODE 0, NOTUBE,
EXTUBE, QUIET, NOBOOT, which is why a reset 3.20 machine comes up in MODE 0 saying "This is not
a language" until FILE and LANG are configured. MOS 3.50 writes a sensible table instead
(FILE 13 ADFS, LANG 12 BASIC, MODE 7, TV 255,1, FDRIVE 3, CAPS, HARD, DELAY 32, REPEAT 8,
IGNORE 10, TUBE, and so on). The emulators each seed their own: B2 language 12, filing system
9, all ROMs inserted, MODE 7, FLOPPY, DELAY 55, REPEAT 3, TUBE, LOUD and INTUBE; BeebEm the same
shape; B-Em's shipped image MODE 7, delay 30, rate 5. Acorn's Welcome Guide recommended BAUD 4,
NOBOOT, CAPS, DATA 4, DELAY 50, NODIR, INTUBE, FDRIVE 3, HARD, IGNORE 10, LANG 12, MODE 7,
TUBE, LOUD, PRINT 1, REPEAT 10, SCROLL, TV 0,1 with FILE 9 (DFS) or 13 (ADFS). Beebium's
seed is a decision (section 4), and R-at-power-on must work as the MOS does it.

### 1.5 Keyboard

93 keys in 13 columns: the Model B's 74 at the same internal numbers, and the keypad at
columns 10-12 (0 &6A, 1 &6B, 2 &7C, 3 &6C, 4 &7A, 5 &7B, 6 &1A, 7 &1B, 8 &2A, 9 &2B, + &3A,
- &3B, / &4A, # &5A, * &5B, , &5C, . &4C, Return &3C, Delete &4B; internal number = row × 16 +
column). The start-up links are gone; boot options come from CMOS. BREAK is wired to reset as
before. OSBYTE &EE and &FE set the keypad's base and SHIFT effect. Beebium's matrix is 10 × 10
and must become 13 columns wide, with new key names in the catalogue and the mapping files.

### 1.6 The hardware interfaces

The Master's distinguishing feature for this programme. The Master's buses: the CPU's own "CD"
data bus carries the RAM, ROM, the I/O controller and the internal Tube; the Peripheral Bus
Controller (PBC, IC21) bridges it to the internal peripheral "BD" bus (the VIAs and the
other 1 MHz chips, the SAA5050's character data, and the modem connector) and to the external
"ED" bus, which is one buffered bus serving both the external Tube and the external 1 MHz bus
at 2 MHz or 1 MHz as the access demands.

- **External Tube (PL6).** A 40-way IDC header carrying D0-D7, A0-A4, R/W, 2E, nIRQ, nTUBE and
  reset, on the PBC's external bus. Registers &FEE0-&FEE7, reached while ITU is clear.
- **Internal Tube (SK1 and SK2).** Two 12-way single-row socket housings into which the Master
  Turbo's 65C102 board and the Master 512's 80186 board plug (the boards carry PL1 and PL2).
  The signals, from Acorn's "2 MHz internal I/O" section: D0-D7 straight off the CPU data bus,
  A0-A2, host Phi2, system reset, host nIRQ (open collector), an 8 MHz reference, the Tube chip
  select and R/W, plus power. There is one register block, &FEE0-&FEE7; ITU steers the single
  nTUBE select to SK1/SK2 or to the PBC and PL6. The unselected coprocessor is not disabled: it
  runs, sitting in its boot code waiting for a message that never arrives. At reset MOS 3.20
  reads CMOS byte 16 bit 2 (INTUBE/EXTUBE), sets ITU accordingly, probes &FEE0, and if nothing
  answers toggles ITU and probes the other side, so a Master with only an external coprocessor
  configured INTUBE still finds it. B2 is the only emulator that models the selection: it
  registers the Tube's handlers in an internal or external scope and the parasite is reachable
  when `acccon.itu` matches the scope it was fitted in, with the host-side Tube IRQ gated the
  same way. The circuit diagram (sheet 1, "INTERNAL TUBE CONN") shows SK1 carrying 0V, the
  8 MHz reference, nIRQ and CD7-CD0, and SK2 carrying A0-A2, buffered R/W, Phi2 in, the INTUBE
  select, reset, and the +5V and 0V battery-backed rails.
- **External 1 MHz bus (PL5).** The B's 34-way connector with the B's pinout (FRED and JIM
  selects, 1MHzE, nNMI, nIRQ, D0-D7, A0-A7, audio), on the PBC's external bus at 1 MHz while
  IFJ is clear. Acorn listed for it the Teletext adapter, IEEE488, Winchester drives (FRED
  &FC40-&FC47), Prestel and the Music 500; Beebium's SCSI and Music 5000 extensions attach here
  today.
- **Cartridge slots (SK3 and SK4).** Two 44-way edge connectors above the keypad: the rear slot
  is ROMs 0 and 1, the front slot ROMs 2 and 3; ROMQA (ROMSEL bit 0) selects the lower or upper
  16K of a 32K cartridge, and the chip selects are decoded from the address's top nibble. A
  cartridge also sees nINFC and nINFD, the FRED and JIM selects, active only while IFJ is set,
  at the 2 MHz Phi2 (hence Acorn's "internal 1 MHz bus runs at 2 MHz"); it has no A14 or A15,
  so it cannot see screen RAM; it gets nIRQ, nNMI, R/W, reset, an audio input into the mixer,
  a machine-detect line (LK12), an optional 16 MHz or 8 MHz clock (LK6) and a light-pen strobe
  (LK21). So a cartridge is both a pair of sideways banks and a FRED/JIM device, and "RAM
  cartridges and memory-mapped I/O cartridges are not guaranteed to work at the same time".
  OSBYTE &6B, meant to select the bus, is broken in MOS 3.20, so software pokes IFJ directly.
  No emulator models cartridges as more than ROM banks, except B2's ADJI joystick cartridge in
  its internal FRED/JIM scope. Beebium's sideways-slots design already reserves
  `runtime_configurable = true` for them.
- **The modem connector (PL12).** A 20-way 0.1 inch ZIF socket housing at the west edge beside
  SK2, fitted on some boards as a socket, on some as header pins, on some not at all (Acorn
  changed it in 1989). It is not the 1 MHz bus: it sits on the internal BD bus with its own
  chip select, nMODEM from the I/O controller, measured on a real Master to decode SHEILA
  &FE80-&FE9F at 1 MHz, the block that holds the disc controller on a B. The circuit diagram
  (sheet 2, PL12 "MODEM") gives its twenty pins: nIRQ, reset, BD0-BD7, nMODEM, A0-A3, 1MHzE,
  R/W, +5V, -5V and 0V; with only A0-A3 the select's 32-byte range is the 16 registers
  mirrored once. A separate five-way header, PL10 "MODEM AUDIO", carries the speech and sound
  outputs and a sound input so the modem board can take the SN76489's output for DTMF dialling
  tones. The data bus is the internal BD bus, time-multiplexed with the teletext character
  data. It has nothing to do with the ACIA or the serial ULA; its point was a modem without
  losing the RS423 port. What
  plugs in: the Beebug Master Modem (1987), the Master AIV / Domesday internal SCSI board (so VFS
  looks for SCSI at &FE80), Simon Inns's Master Extra User Ports board, and hobbyist VIAs. No
  Acorn-branded internal modem was found. The Compact deleted it.
- **Econet module (SK5 and SK6).** The 68B54 ADLC is not on the motherboard; it is a daughter
  board on a 19-way system-bus socket (A0-A3, D0-D7, R/W, Phi2 at 2 MHz, nADLC for &FEA0-&FEAF,
  nNETINT ORed into NMI by the I/O controller, reset, power) and a 5-way socket to the DIN. The
  NMI mask is INTOFF/INTON at &FE38/&FE3C in the I/O controller. The station number is CMOS
  byte 0, not a register, so Beebium's `--station` must seed CMOS.
- **Other headers.** PL8 battery (3-way; a lithium cell on early boards, three AA cells later,
  with LK4 for the clock's alarm IRQ, not fitted); PL11/PL24 keyboard (2 x 15); PL7 the
  keyboard's second connector on the diagram; PL1/PL2 the two drive connectors; PL3 printer;
  PL4 user port; PL9 speaker; PL10 modem audio; PL13 the RGB/TV feed. No speech socket exists,
  though the SPEECH line still runs to the modem audio header and the cartridge slots.
- **The keyboard matrix.** The circuit diagram's third sheet draws the whole matrix: the
  Model B's ten columns, and the keypad's keys in three more columns, with the primed keys
  (4' 5' 2' / 0' 1' 3' / #' *' ,' / /' DELETE' .' / +' -' RETURN' / 8' 9' / 6' 7') in rows 0-6
  of columns 10-12, matching the manual's internal key numbers.
- **ROM sockets.** IC24 holds the 1 Mbit MOS ROM (LK9); IC27 is ROM 8 (16K); IC41 takes a 32K
  ROM as banks 4/5 with LK18 East, IC37 banks 6/7 with LK19 East; West gives the sideways RAM
  banks instead (the Service Manual and Advanced Reference Manual swap the two sockets; the
  Reference Manual, the Hardware Specification and the board agree on IC41 = 4/5, IC37 = 6/7).

### 1.7 The 65SC12

CMOS 6502 without the Rockwell bit instructions: BRA, PHX/PHY/PLX/PLY, STZ, TRB/TSB, INC/DEC A,
the (zp) mode, JMP (abs,X), BIT with more modes. Timing differences that the dp111 suite and
beebjit document: decimal-mode ADC/SBC take an extra cycle and set the flags correctly;
read-modify-write does read, read, write; a page-crossing indexed read makes no dummy read;
ASL/ROL/LSR/ROR abs,X take 6 cycles plus one on a page crossing while INC/DEC abs,X take 7;
JMP (ind) is 6 cycles with the page-wrap bug fixed; the undocumented opcodes become NOPs of
defined lengths (1-cycle for x3/x7/xB/xF, 2-cycle for x2, &5C 8 cycles, &DC/&FC 4); IRQ and BRK
clear D (BeebEm gets this wrong). Beebium's `Cmos65C02` CPU policy uses the 6502 library's
CMOS configuration, which is this instruction set, so no new core is needed, but the dp111
timing harness has no CMOS build without the Rockwell instructions; beebjit's
`65C12timing1M.ssd` and its `make_master_rom.c` show how to get one.

## 2. How the emulators approach it

| | B2 | B-Em | BeebEm | beebjit |
|---|---|---|---|---|
| Structure | one `BBCMicroType` data object per model: paging function, big-page table, stretch ranges, I/O flag mask; update loop templated on the model | `if (MASTER)` branches; MODEL struct from config | scattered `MachineType ==` checks, inconsistent between 128 and ET | one `is_master` flag through `bbc.c` |
| CPU | CMOS config (65SC12) | 65C02 without Rockwell, no WAI/STP | 65C12 tables; D not cleared on IRQ (bug) | 65C12 interpreter only; most careful timing |
| Shadow rule | (Y and X) or (not Y and E), by opcode page | by fetch PC per instruction | by PrePC per access | as B2; window test off by one at edges |
| ROMSEL/ACCCON readable | yes | Master only | yes | yes |
| IRR | stored, unused | stored | raw IRQ source until cleared | stored |
| TST | reads from ROM | reads from ROM | reads from ROM | stored |
| ITU / IFJ | select handler scopes; FRED/JIM unstretched when IFJ | ignored | ignored | ignored |
| RTC | MC146818 model, no IRQ/UIP, per-config nvram in json | host time + offset, cmos.bin with offset in bytes 0/2/4/6 | host time + offset, bytes 14-63 in preferences; binary-mode write bug | read-only table, writes logged |
| 1770 latch | &FE24 write-only, read = stale bus; bit 3 ignored | &FE24-27 / &FE28-2B; &FE2C-2F undecoded | &FE24 / &FE28-2F; reset bit ignored | &FE24-27 / &FE28-2B; read returns 0 (TODO: stale bus) |
| Keypad | columns 10-14 | 13 columns | 16 columns, keypad 10-12 via kmap | none |
| Cartridges | ROM banks; ADJI joystick only | ROM banks, no write protection | ROM banks | none |
| Stale data bus | yes | no | no | no |
| Teletext &3C00 alias | none on Master | &7C00 base | &7C00 wrap | none on Master |

Bugs and gaps worth not repeating: B2's sideways RAM image copy loop is empty and its
`SetTime` clamps seconds to 1-60; BeebEm converts binary-mode clock writes to BCD and never
clears D on interrupts; B-Em does not clear its ACCCON mirror on reset; beebjit's VDU-code window
uses the post-instruction PC; none models UIP; none persists CMOS as the machine's own
battery-backed state in the way Beebium's `nvram/` does.

Three architectural lessons: B2's per-model data object (paging function, stretch table, I/O
scopes) is the cleanest separation and maps onto Beebium's hardware-policy template; B2's
scoped I/O registration (external and internal FRED/JIM, external and internal Tube) is the one
working model of ITU and IFJ; and every emulator treats the 128K MOS as one file split into MOS
plus banks 9-15, which Beebium's ROM loading should do too rather than ask for seven files.

## 3. What Beebium adds and changes

Reuse first. A variant is a `Machine<CpuPolicy, Hardware>` alias, a one-line main, an entry in
the model list, a CMake server target and a system preset (`docs/plans/atpl-sidewise-machine.md`
is the worked precedent). The `Cmos65C02` CPU policy, `Mc146818Rtc`, `BatteryBackedUnit` and the
`nvram/` persistence, `TubeSocket`, `OneMHzBusPort`, `SerialSocket`, the Econet socket, the
1770 controller, `ConfigurableBankedMemory`, the sideways slot topology framework and the
debugger's paging-variable machinery all carry over. The client specification requires that a
new variant needs no client change, and the clients take model names and executables from the
server's catalogues.

New or changed, by component:

| Component | Work | Builds on |
|---|---|---|
| `ModelBMaster128Hardware` policy | memory map with LYNNE, HAZEL, ANDY and four RAM banks; ROMSEL with bit 7; ACCCON with all eight bits readable; the Master shadow rule as an opcode-fetch latch; display from LYNNE on D; TST read routing | B+ `HasOpcodeFetchLatch`, Integra-B overlays |
| CPU policy | `Cmos65C02` as is | existing |
| 1 MHz stretch map | the Master's SHEILA ranges; FRED/JIM unstretched when IFJ | `BusStretching` |
| Stale data bus | unmapped reads return the last video fetch, if measured so | video fetch path |
| System VIA peripheral | a Master subclass: PB6/PB7 as CE/AS outputs, latch bits 1/2 as R/W/DS, the clock's data ANDed onto port A in read mode; no speech | `SystemViaPeripheral`, `AddressableLatch` |
| Clock and CMOS | `Mc146818Rtc` wired to the VIA; Master seed (MOS 3.20 defaults); `BatteryBackedUnit` `master-128-cmos` or similar; device inspection | Integra-B's two units |
| Keyboard | 13-column matrix; keypad key names in the catalogue and mapping files; no links; CMOS boot options | `KeyboardMatrix`, `KeyboardMapping` |
| Disc | 1770 at &FE24/&FE28 with the Master latch layout; a third drive select, if modelled; no 8271 | `Acorn1770DiscController` |
| ADC | at &FE18 | `AnaloguePortDevice` |
| Econet | INTOFF/INTON at &FE38/&FE3C; station from CMOS byte 0; `--station` seeds CMOS | `EconetSocket` |
| Tube | internal and external scopes selected by ITU; a second attachment id, or one socket with an internal/external position; host IRQ gated by the selection | `TubeSocket`, attachment points |
| 1 MHz bus and cartridges | FRED/JIM routed by IFJ between the external port and the cartridge bus; cartridge slots as runtime-configurable sideways pairs that also own FRED/JIM pages and see IRQ, NMI and the audio mixer; the modem connector as a third attachment point decoding nMODEM at &FE80-&FE9F on the internal 1 MHz bus | `OneMHzBusPort`, `SlotTopology`, attachment points |
| Sideways topology | banks 9-15 ROM from the MOS image, locked; 4-7 RAM, or 32K ROM pairs at IC37/IC41 via LK18/LK19; 8 socket IC27; 0-3 cartridges | B+ 128K W/X/Y/Z, motherboard links |
| ROMs | MOS 3.20 and 3.50 as eight 16K files each in `roms/`, one preset per MOS; the MOS half as the policy's default MOS ROM and banks 9-15 as locked default sideways images; a manifest entry by SHA-1 if wanted | DFS manifest pattern |
| Debugger | the reserved names `accd`, `acce`, `accx`, `accy`, `accitu`, `accifj`, `acctst`, `accirr`, `shadow_access`, `bank`, `romsel` | `docs/discussion/paging-qualified-breakpoints.md` |
| Identity | OSBYTE 0 = 3 comes from the MOS; `describe-preset-schema` and `machine.json` need the model | model list |
| Packaging | server target, CI artifact lists and smokes, Scoop, the Python server package's variant list (which today names only four of seven), the Python client's `VARIANTS`, CLAUDE.md | existing lists |
| Tests | 65SC12 dp111 timing build; ACCCON rule table tests; RTC through the VIA against the manual's sequence; keypad; 1770 latch; CMOS persistence round trip; MOS 3.20 boot to `BASIC` with `*CONFIGURE` and `*STATUS`; the Stratagem game's Master build | existing harnesses |

## 4. Decisions

Taken with Rob on 2026-11-19; the plan for the first round follows from them.

1. **Scope of the first variant.** Basic Master 128; the external Tube and 1 MHz bus as today;
   the internal Tube, the two cartridge slots and the modem connector modelled as attachment
   points from the start (the policy routes by ITU and IFJ and decodes nMODEM) with no fittings
   in the first round beyond cartridge ROM pairs. The internal SCSI at &FE80 is the natural first
   modem-connector fitting later, since the SCSI extension exists.
2. **ROM provision.** MOS 3.20 and 3.50 are both in `roms/` as per-bank files, each a distinct
   Master preset; **MOS 3.20 is the default** when a user picks the Master 128, 3.50 a bundled
   alternative.
3. **CMOS seed.** The configuration Acorn's Welcome Guide tells a new owner to enter (section
   6 sources: 8bs artcmos, stardot 7582): BAUD 4, NOBOOT, CAPS, DATA 4, DELAY 50, NODIR, INTUBE,
   FDRIVE 3, HARD, IGNORE 10, LANG 12, MODE 7, TUBE, LOUD, PRINT 1, REPEAT 10, SCROLL, TV 0,1,
   with FILE 13 (ADFS, bank 13) in place of the guide's misprinted FILE 15, every ROM insert bit
   set and station 0; not MOS 3.50's table and not an all-zero image. If HARD makes ADFS wait
   for a Winchester that is not there at boot, FLOPPY replaces it and the plan says so. The persistence unit is `Mc146818BatteryUnit` with a Master name and seed; R held at
   power-on zeroes bytes 1-49 and sets 6-7 to &FF exactly as the MOS does, because that is the
   MOS running, not the emulator.
4. **Clock time source.** Host civil time plus a saved offset, as the Integra-B and the user-port
   clock do; the existing command-line clock option selects emulated time for tests.
5. **Stale data bus.** Model it in the first round: unmapped SHEILA addresses and empty sideways
   banks read as the last byte the video system fetched (&20 in Mode 7), as B2 does. ANFS 4.25's
   ADLC probe and read-invert-write sideways RAM detection depend on it.
5a. **Wait states versus clock stretching.** The B stretches the CPU clock; the Master holds
   RDY with Phi2 constant. The cycle cost is the same, so Beebium's `BusStretching` stands with
   the Master's address map, but peripherals clocked from Phi2 (the Econet module) and the
   Tube see a steady 2 MHz, which the Tube contract must respect.
6. **Cartridges.** In the first round a cartridge is a runtime-configurable pair of 16K ROM
   images in the slot's two banks. The IFJ routing of FRED/JIM to the cartridge bus and the IRQ,
   NMI and audio lines wait for a cartridge that needs them.
7. **Debugger vocabulary.** The reserved names `accd`, `acce`, `accx`, `accy`, `accitu`,
   `accifj`, `acctst`, `accirr` (with `bank`, `romsel`, `shadow_access`) are adopted; the final
   list is posted on #189 so the downstream project can object before the release.

## 5. Facts to verify on a real Master

Rob's Master is available (the November ADC trials ran on it). Each of these is a short BASIC or
assembler test:

1. The shadow rule's window: an access from code at &BFFF, &C000, &DFFF and &E000 with
   Y = 0, X = 0, E = 1; operand fetches versus data accesses (the SYNC rule says operands are
   redirected); and the Y = 1, E = 1 case with code executing out of HAZEL, which no source
   has measured and the emulators split on.
2. What ROMSEL and ACCCON read back, bit by bit, including ROMSEL bits 4-7; no hardware
   confirmation of ROMSEL readback exists.
3. What an unmapped SHEILA address, the 1770 latch, and &FE2C-&FE2F read, in Mode 7 and Mode 0.
4. The 1770 latch's bit 3 (DS2) and bit 2 (RES) behaviour, and whether the 1770 registers are
   1 MHz (the Reference Manual) or 2 MHz (the emulators).
5. The clock: the datasheet settles the address latch (the falling edge of AS) and the UIP
   window (244 us before a 1984 us update cycle), so what remains is what the board does with
   the chip: whether the power-sense pin is held high so VRT reads 1 (and whether MOS 3.20 ever
   reads register D), whether Break reaches the chip's RESET pin (the diagram shows RST there),
   and whether LK4 is made on this machine.
6. The ADC's decode width at &FE18 (8 bytes? through &FE20?), and the Video ULA's at &FE20.
7. 65SC12 timing through dp111's 65C12 suite at 1 MHz and 2 MHz, as the oracle for the
   Beebium build, and the extra IRQ entry cycle.
8. Keypad matrix positions against the manual's numbers.
9. The display quirk with D set and screen addresses below &3000.
10. TST's effect, which Acorn forbids and no one has measured.
11. Whether PL12 is fitted on Rob's board, and what nMODEM decodes.

## 6. Sources

Local: the B2, B-Em, BeebEm (Windows and Mac) and beebjit trees; `docs/manuals_text/
BBC_Master-New-Advanced-User-Guide`; the MC146818 datasheet (Motorola 1988, Freescale reprint)
as `docs/datasheets/MC146818.pdf`; the Master 128 main PCB circuit diagram, Acorn drawing
0143,000/C as redrawn for stardot in April 2024 (two sheets plus the keyboard matrix), kept
as `docs/manuals/BBC Master 128 Circuit Diagram APR2024.pdf` beside the other manuals (not
committed, as none of them is); Beebium's own `docs/sideways-slots.md`,
`docs/discussion/paging-qualified-breakpoints.md`, `docs/machine-directory.md`,
`docs/plans/atpl-sidewise-machine.md`. The per-emulator notes with file and line citations are
working papers, not in the repository.

Acorn: Master Series Reference Manual Part 1 (0443,001; retrosix.wiki), Master Series Service
Manual (0443,004; chrisacorns), Master Hardware Specification (chrisacorns), the Advanced
Reference Manual for the BBC Master Series (Acorn/Watford; theoddys.com), the Master 128
circuit diagram (theoddys.com, image), Application Notes 003, 031 and 042, Customer Service
News 2 (May 1989), the Master 512 Technical Guide appendices. MOS 3.20 and 3.50 source
disassemblies: mdfs.net.

Community: beebwiki (Master 128, CMOS configuration RAM allocation, Paged ROM); sprow.co.uk
(cartridges, Compact CMOS bits); 8bs.com (Inside the Master 128, CMOS reset, ROM socket hint);
chrisacorns (Beebug Master Modem, Master Turbo); domesday86.com (65C102 board); the
econet-upurs project (module pinout); kieranhj's hardware-measurement wiki (shadow RAM, paged
ROM, CMOS, cycle stretching, with its own caveats); Wikipedia (BBC Master). stardot threads:
9403 (PL12 and the AIV SCSI), 11369 (user ports on PL12), 16335 (unmapped reads return &20),
16510 (two coprocessors and ITU), 28080 (Tube ULA, 65SC12 Phi2), 17243 (New AUG errata),
26017 (OSBYTE &6B broken), 30245 (cartridge port), 15729 (Planet Nubium), 10748 (IRQ entry),
7582 and 24936 (CMOS settings and R reset), 25129 and 26791 (RDY wait states). Emulator
sources read online: jsbeeb 6502.js and MAME bbcm.cpp, beside the local four.
