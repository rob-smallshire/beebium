# The BBC Master 128

The BBC Master 128 as Beebium builds it: the `beebium-master-128` server, model id
`master-128`, hardware policy `Master128Hardware`
(`src/core/include/beebium/Master128Hardware.hpp`). It covers:

- the memory, the paging registers ROMSEL and ACCCON, and the rules by which an access
  reaches main RAM, LYNNE, HAZEL or ANDY;
- the SHEILA map, the 1 MHz map and the stale data bus;
- the MC146818 clock on the System VIA, its CMOS seed and the battery-backed unit;
- the keyboard and its keypad;
- the 1770 and its latch;
- the two Tubes, the 1 MHz and cartridge buses, the cartridge slots and the modem connector;
- the presets, the debugger vocabulary and the tests;
- what is simplified and what is still to be measured.

The tracking issue is #189. The research behind every decision is
`docs/discussion/master-128-research.md` (cited below as "research"), whose section 4 holds
the decisions and section 5 the measurements still to make on a real Master. The round's
plan is `docs/plans/master-128-round-1.md`.

## What is modelled and what is not

Modelled:

- The 65SC12 instruction set through the `Cmos65C02` CPU policy
  (`M6502_cmos6502_config`: the CMOS instructions without Rockwell's bit instructions).
- 128K of RAM: 32K main, LYNNE, HAZEL, ANDY and sideways RAM banks 4-7.
- The MOS 3.20 ROM as its eight 16K parts, the MOS half and banks 9-15; MOS 3.50 as a preset.
- ROMSEL and ACCCON, both readable, with X, E (the SYNC rule), Y, D, TST, IRR, ITU and IFJ.
- The stale data bus on unmapped SHEILA and empty sideways banks.
- The MC146818 on the System VIA in the datasheet's Motorola-mode protocol, kept in
  `nvram/master-128-cmos.bin`.
- The 13-column keyboard with the keypad.
- The 1770 with the Master's latch.
- Econet's INTOFF and INTON at &FE38 and &FE3C, and its station number in CMOS byte 0.
- The ADC at &FE18.
- The external and internal Tubes.
- FRED and JIM routed by IFJ.
- Two cartridge slots, filled by cartridge extensions.
- The modem connector's select.
- LK18 and LK19.

Not modelled, or simplified:

- The extra cycle the Master takes to enter an IRQ handler (research section 1.2) is not
  modelled: it is a CPU library question, left open.
- The clock's IRQ output reaches the CPU only through link LK4, unmade as shipped, so it is
  not wired.
- One coprocessor at a time, on either Tube.
- Nothing decodes FRED or JIM on the cartridge bus yet, and nothing is fitted to the modem
  connector.
- A cartridge is fitted at launch and changed with a relaunch.
- The Master Compact, Turbo, 512 and ET are not separate models. The internal Tube with the
  65C102 plugin is a Master Turbo in all but name.

## Memory

| Address | What answers |
|---|---|
| &0000-&7FFF | 32K main RAM; LYNNE (20K) over &3000-&7FFF by X and E, below |
| &8000-&BFFF | the sideways bank ROMSEL selects; ANDY (4K) over &8000-&8FFF while ROMSEL bit 7 is set |
| &C000-&FFFF | the MOS ROM; HAZEL (8K) over &C000-&DFFF while ACCCON Y is set; I/O at &FC00-&FEFF |

The sideways banks (`Master128Hardware::slot_topology`):

| Banks | What they are |
|---|---|
| 0, 1 | the rear cartridge slot: empty unless a cartridge fills them |
| 2, 3 | the front cartridge slot: the same |
| 4-7 | sideways RAM; with LK18 East (4, 5) or LK19 East (6, 7) the halves of a 32K ROM in IC41 or IC37, filled with `--sideways` |
| 8 | the IC27 ROM socket, empty by default |
| 9-15 | the MOS ROM's sideways parts, ROM only and never empty: DFS 2.24, ViewSheet B1.0, Edit 1.00, BASIC IV, ADFS 1.50, View B3.0, Terminal 1.20 (`DEFAULT_SIDEWAYS_ROMS`, `DEFAULT_LANGUAGE_SLOT` 12) |

`docs/sideways-slots.md` has the topology in the terms the other models use.

### ROMSEL (&FE30-&FE33)

Bits 0-3 select the bank and bit 7 pages ANDY. The register is readable, and all eight bits
read back as written: no measurement of ROMSEL's bits 4-6 exists, so this is an assumption
(research section 5 item 2).

### ACCCON (&FE34-&FE37)

All eight bits are stored and read back:

| Bit | Name | Set |
|---|---|---|
| 7 | IRR | holds the IRQ line low, as a level, until cleared |
| 6 | TST | reads of &FC00-&FFFF return the MOS ROM instead of the I/O (the emulators' consensus; unmeasured, section 5 item 10); writes still reach the I/O |
| 5 | IFJ | &FC00-&FDFF is the cartridge bus at 2 MHz, not the external 1 MHz bus |
| 4 | ITU | &FEE0-&FEFF and the host Tube IRQ go to the internal Tube |
| 3 | Y | HAZEL over &C000-&DFFF |
| 2 | X | every access to &3000-&7FFF reaches LYNNE |
| 1 | E | an instruction fetched from &C000-&DFFF reaches LYNNE at &3000-&7FFF |
| 0 | D | the display shows LYNNE |

Break and power-on clear ROMSEL and ACCCON.

### The shadow rule

`Master128Hardware::lynne_selected`. With X set, every access to &3000-&7FFF reaches
LYNNE. With X clear and E set, the access reaches LYNNE when the instruction making it was
fetched from &C000-&DFFF. The I/O controller's view of SYNC is a flag latched at each
opcode fetch (`note_opcode_fetch`, reported by `CpuBinding` before the fetch is made),
which holds for the instruction's operand and data accesses and for an interrupt entry.
An opcode fetch is never redirected, since the flag it sets is true only for addresses
outside &3000-&7FFF.

X wins over E, so with X set and E clear the VDU driver's own writes land in LYNNE (the
Planet Nubium corner, research section 1.1). Y leaves E alone, as Acorn describes it;
`Y_SUPPRESSES_E` is the constant to flip if a real Master shows the B2 and beebjit rule
instead (section 5 item 1).

An access made without an instruction behind it, a debugger's or a client's
(`read`, `write`, `peek`), sees LYNNE by X alone. `read_with_pc` and `write_with_pc` make
the access as code at a given PC would.

### The display

With D clear the CRTC reads main RAM. With D set it reads the second 64K bank throughout:
LYNNE at &3000-&7FFF, and below &3000 ANDY (&0000-&0FFF) and HAZEL (&1000-&2FFF), as
beebjit has it against a hardware-verified test (`peek_video`; section 5 item 9). There is
no Mode 7 alias at &3C00: teletext addresses are &7C00-&7FFF, as on every model.

## SHEILA

| Address | Device |
|---|---|
| &FE00-&FE07 | CRTC |
| &FE08-&FE0F | ACIA |
| &FE10-&FE17 | Serial ULA |
| &FE18-&FE1F | uPD7002 ADC (registers mirrored every four) |
| &FE20-&FE23 | Video ULA (decodes four bytes; no INTON) |
| &FE24-&FE27 | 1770 drive control latch: write only, a read is the stale bus |
| &FE28-&FE2B | 1770 registers |
| &FE2C-&FE2F | undecoded: the stale bus (as B-Em has it; section 5 item 3) |
| &FE30-&FE33 | ROMSEL |
| &FE34-&FE37 | ACCCON |
| &FE38-&FE3B | Econet INTOFF: any access; a read is the stale bus |
| &FE3C-&FE3F | Econet INTON: any access; a read is the stale bus |
| &FE40-&FE5F | System VIA |
| &FE60-&FE7F | User VIA |
| &FE80-&FE9F | the modem connector's select (nMODEM): nothing fitted, the stale bus, A0-A3 mirroring |
| &FEA0-&FEBF | Econet ADLC (the stale bus with no Econet fitted) |
| &FEC0-&FEDF | unmapped: the stale bus |
| &FEE0-&FEFF | the Tube ITU selects (the stale bus with nothing fitted) |

### The 1 MHz map

`Master128Hardware::is_1mhz_access`, which `CpuBinding` uses in place of the Model B's map.
Accesses to &FE00-&FE1F, &FE28-&FE2B and &FE40-&FE9F, and to FRED and JIM while IFJ is
clear, wait for the 1 MHz clock, at the Model B's cost in cycles. This is beebjit's and B2's
table; whether the 1770's registers are 1 MHz or 2 MHz is unmeasured (section 5 item 4). The
Master holds RDY with Phi2 constant where the B stretches the clock; the cost in cycles is
the same, so Beebium's stretching stands for it.

### The stale data bus

A read of an unmapped SHEILA address, the 1770 latch, an empty Tube or ADLC, or an empty
sideways bank returns the byte the video system last fetched (`stale_bus()`): &20 across a
Mode 7 screen of spaces, &00 in a cleared Mode 0. `VideoBinding` reports every character
fetch to the policy (`note_video_fetch`), whether or not the picture is rendered. ANFS's
ADLC probe and read-invert-write RAM detection depend on it (research section 1.2).

## The clock and the CMOS RAM

An MC146818 (`Mc146818Rtc`) hangs off the System VIA (`MasterSystemViaPeripheral`):

- port A is its multiplexed address and data bus;
- PB6 is chip enable and PB7 address strobe;
- addressable latch bit 1 is R/W (high to read) and bit 2 the data strobe.

The bus is the datasheet's Motorola mode:

- the address latches on AS falling with CE high;
- a read drives port A while DS is high with R/W high, the register read once as DS rises;
- a write latches port A as DS falls with R/W low.

CE gates both strobes. The New Advanced User Guide's `cmosaccess` routine (19.6) is the test
sequence. The clock runs on host time plus a saved offset; `--rtc` (the keys of
`--integra-rtc`: `clock=emulated`, `time=`, `offset=`) gives an emulated clock for tests.
The chip's RESET pin is on the system reset, so power-on and Break clear PIE, AIE, UIE,
SQWE and the flags and leave the time and the RAM.

### The seed

`master_128_cmos_seed::welcome_guide` (`Master128CmosSeed.hpp`): the configuration Acorn's
Welcome Guide tells a new owner to enter, with FLOPPY for its HARD and FILE 13 for its
FILE 15, every ROM inserted and station 0. MOS 3.20's `*STATUS` reads it as:

```
Baud     4      No Boot       Caps         Data     4     Delay    50
No Directory    Internal Tube FDrive   3   File     13    Floppy
Ignore   10     Lang     12   Mode     7   Tube           Loud
Print    1      Repeat   10   Scroll       TV       0,1
```

Its encodings were settled against that `*STATUS`:

- BAUD n is held as n - 1 in byte 15 bits 2-4;
- byte 16 bit 2 set is EXTUBE, so INTUBE is the bit clear;
- byte 11 bit 7 set is FLOPPY.

FILE 13 rather than the guide's FILE 15: bank 15 is Terminal, not a filing system, and
MOS 3.20 does not fall back from it; it starts on tape.

### The battery-backed unit

The clock and its CMOS RAM are one chip and one unit, `master-128-cmos` (hardware descriptor
"Master 128: MC146818", layout version 1), a `Mc146818BatteryUnit`:

- it serialises the 64-byte register image and an 8-byte little-endian offset from host
  time;
- a saved machine keeps it in `nvram/master-128-cmos.bin`
  (`docs/machine-directory.md`);
- `*CONFIGURE` therefore survives Ctrl-Break, a Save and a relaunch;
- `reset-battery` returns it to the seed.

R held at power-on is MOS 3.20's own CMOS reset, which Beebium leaves to the MOS: it prints
"CMOS RAM reset", zeroes bytes 1-49 and sets bytes 6 and 7 to &FF. MOS 3.20 prints the year
as 19yy whatever it is (2026 is "1926"); that is the MOS.

### The Econet station

The station number is CMOS byte 0, which the filing system reads as it starts:

- `--station` writes that byte (`set_cmos_station`);
- a station set through `EconetService` is written there too;
- at every power-on and Break the socket takes its number from it (`adopt_station`);
- so the guest's configuration and Econet's status agree after a Break.

## The keyboard

`KeyboardMatrix::MASTER_128_LAYOUT`: 13 columns and no start-up links. The keypad's 19 keys
are in columns 10-12 at the internal numbers of research section 1.5. They are in the key
catalogue as `Keypad0`-`Keypad9`, `KeypadPlus`, `KeypadMinus`, `KeypadDivide`, `KeypadHash`,
`KeypadMultiply`, `KeypadComma`, `KeypadPeriod`, `KeypadReturn` and `KeypadDelete`, named
only, so typing a digit still presses the main keyboard's key.

`KeyboardService` lists and presses only the keys a machine's keyboard has, so the keypad is
the Master's alone. The Logical built-in mapping sends the host's numeric keypad to it, and
a machine without one passes over those entries (`docs/keyboard-mapping-files.md` 3.6).

The Master's boot options are in CMOS, so its keyboard has no links:

- `--links`, `--screen-mode` and `--auto-boot`, and a preset's `startup`, are refused with a
  sentence;
- the links RPCs return FAILED_PRECONDITION;
- the preset schema has no startup section (`HasStartupLinks`).

## The disc controller

The 1770 is on the motherboard: `Acorn1770DiscController` with `LatchLayout::Master`, and two
drives. There is no 8271 and no socket, so `--fdc` does not apply. The latch at &FE24:

| Bit | Name | |
|---|---|---|
| 0 | DS0 | drive 0 select |
| 1 | DS1 | drive 1 select |
| 2 | RES | reset, active low: the 1770 resets as it falls |
| 3 | DS2 | a third drive select, with no drive (`WD1770::select_no_drive`) |
| 4 | SEL | side |
| 5 | DDEN | density, 1 = single |

`docs/disc-subsystem.md` sets this beside the Model B board's layout. The 1770's NMI is
sampled before each 1 MHz tick, as on the B+.

## The two Tubes

The external Tube (PL6) is `tube_socket` and the attachment point `tube`, unchanged for every
existing caller. The internal Tube (SK1/SK2) is `internal_tube_socket` and the attachment
point `internal-tube` ("Internal Coprocessor"). ACCCON ITU steers &FEE0-&FEFF and the host
Tube IRQ to one of them. `Machine` runs every Tube socket in host time
(`for_each_tube_socket`), so a coprocessor on the Tube the host cannot reach runs on,
unreached. `TubeService` and Tube inspection report the Tube the coprocessor is fitted to
(`fitted_tube_socket`).

A coprocessor extension attaches to one of `tube` and `internal-tube`
(`attaches_to_one_of`), named by its instance's `at`; without `at` it goes on the external
Tube, as on every other model (`docs/peripheral-extension-framework.md`,
`docs/preset-schema.md`). MOS 3.20 probes the Tube its CMOS names (the seed says INTUBE),
toggles ITU and probes the other, so a coprocessor on the external Tube is still found.
Beebium serves one coprocessor at a time: a coprocessor on each Tube is refused with a
sentence.

## The buses, the cartridge slots and the modem connector

**FRED and JIM.** While IFJ is clear, &FC00-&FDFF is the external 1 MHz bus
(`one_mhz_bus()`, the attachment point `1mhz-bus`), where the SCSI and Music 5000 extensions
attach unchanged. While IFJ is set, it is the cartridge bus at 2 MHz (`FredJimRegion`).
Nothing decodes FRED or JIM there yet, so a read is the stale bus, and the external bus is
not reached.

**The cartridge slots.** `cartridge-rear` (banks 0 and 1) and `cartridge-front` (banks 2 and
3) are attachment points. A cartridge is an extension placed at one with `at`, and it
provides the slot's sideways banks:

- it fits itself to the slot's `CartridgeSlot` port (`extension/CartridgeSlot.hpp`) as a
  `SidewaysBankProvider` (`devices/SidewaysBankProvider.hpp`);
- `ConfigurableBankedMemory::attach_provider` hands it the two banks;
- the CPU, the debugger and `SidewaysService` all ask the cartridge for them;
- ROMQA (ROMSEL bit 0) chooses between the two banks.

`SidewaysService` shows the banks with the cartridge's image names, read only. `ConfigureSlot`
on one is refused with a sentence saying how to fit a cartridge at launch.

The built-in `rom-cartridge` is the simplest cartridge: `low` and `high` are the ROMs in its
two 16K sockets, on the slot's even and odd banks. Each is a ROM name on the ROM search path
or a path (the parameter's `resource: "rom"`). Acorn's Master ROM Cartridge
(`docs/master-128/README.md`) answers with a single chip on its own bank only; Beebium lets
`low` given alone answer on both banks, a convenience for 16K cartridges wired that way. A
RAM cartridge, a 27513 taking its page from a write, or a cartridge with FRED/JIM I/O follows
the same shape with a writable provider or bus handlers beside.

**The modem connector.** The attachment point `modem-connector` is declared, with nothing to
fit yet. nMODEM decodes &FE80-&FE9F at 1 MHz, and the connector carries A0-A3 only, so the
32 bytes are 16 registers mirrored once.

**LK18 and LK19.** `--motherboard-link lk18=east` and `lk19=east`
(`Master128Hardware::MotherboardLinks`) turn banks 4/5 and 6/7 from sideways RAM (West, as
shipped) into the halves of a 32K ROM in IC41 and IC37. The even bank is the low half, and
the banks are filled with `--sideways`.

## Presets

| Preset | What it is |
|---|---|
| `master-128` | MOS 3.20 and its ROM set, the Welcome Guide's CMOS (the model's default) |
| `master-128-mos-3-50` | MOS 3.50 and its ROM set: DFS 2.45, ViewSheet B1.01, Edit 1.50r, BASIC IV r32, ADFS 2.03, View B3.3, Terminal 1.20 for 3.50 |
| `master-128-65c102-copro` | MOS 3.20 with the 65C102 Co-processor on the internal Tube, a Master Turbo in all but name |

`docs/deployment.md` tabulates the ROM files. `describe-preset-schema` gives the model's
attachment points, sideways topology and default ROMs.

## The debugger

Breakpoint and watchpoint conditions (`debug_variables`;
`docs/discussion/paging-qualified-breakpoints.md`) can name:

- `romsel` and `bank`;
- `andy` (ROMSEL bit 7);
- `acccon` and its bits `accd`, `acce`, `accx`, `accy`, `accitu`, `accifj`, `acctst` and
  `accirr`;
- in watchpoints, `shadow_access`: whether the access under test reached LYNNE, decided by
  X and E as above.

The memory regions are `main_ram`, `lynne`, `hazel`, `andy`, `mos_rom` and `bank_0` to
`bank_15`. `GetRealTimeClockState` serves the clock's registers. No inspection RPC reads
ROMSEL and ACCCON yet: both are readable at &FE30 and &FE34, and the condition names test
them.

## Testing

| Test | What it covers |
|---|---|
| `tests/test_master_128_hardware.cpp` | each paging rule: ROMSEL and ACCCON readback, ANDY, HAZEL, X, E at &BFFF, &C000, &DFFF and &E000 and across an opcode fetch, the X=1 E=0 corner, Y with E, D below &3000, TST, IRR; the stale bus; the 1 MHz map with IFJ; Break; the ADC; INTOFF/INTON and the CMOS station; the 1770's latch and registers; ITU steering; IFJ routing; the modem mirror; LK18/LK19; a cartridge provider |
| `tests/test_master_128_clock.cpp` | the clock through the System VIA: a CMOS byte written and read, CE gating, the DS-edge write, the clock driving port A only while a read's DS is high, RESET on Break, the seed's bytes, the unit |
| `tests/test_master_128_boot.cpp` | MOS 3.20 from the real ROMs: Mode 7 BASIC on ADFS, every `*STATUS` line, `*TIME`, `*CONFIGURE MODE` across Ctrl-Break, R at power-on, the keypad typing |
| `tests/test_master_128_dp111_timing.cpp` | dp111's 65C12 timing suite (1 MHz build): 0 failures |
| `tests/test_keyboard_matrix_layout.cpp`, `tests/test_grpc_keyboard_master_128.cpp` | the 13-column layout, the keypad in the catalogue, `KeyboardService` on each keyboard |
| `tests/test_acorn_1770_controller.cpp` | the Master latch's bits |
| `tests/test_attachment_plan.cpp`, `tests/test_extension_registry.cpp` | placement with `at` and the Tube a context gives |
| `tests/test_condition_variables.cpp` | the debugger names and the PC-aware accesses |
| `clients/beebium-python-client/tests/test_master_128.py` | through the server: the presets booting; `*CONFIGURE` across a Save and relaunch; the links refused; the schema; ADFS and DFS on floppies; the fdc placeholder; `--station`; both Tubes and the cross-probe; two coprocessors and a Model B's internal Tube refused; cartridges and LK18 through `SidewaysService` |
| `cmake/InstallSmokeTest.cmake`, `scripts/smoke-installed-tree.sh` | the installed server boots the `master-128` preset |

## Decisions still unverified

Each is coded as the stated assumption, with a comment naming it, so a hardware measurement
can change it (research section 5):

- ROMSEL bits 4-6 read back as written (item 2).
- Y does not suppress E (`Y_SUPPRESSES_E`, item 1).
- TST reads &FC00-&FFFF from the MOS ROM (item 10).
- D with screen addresses below &3000 shows ANDY and HAZEL (item 9).
- The stale bus is the last video fetch, and &FE2C-&FE2F is undecoded (item 3).
- The 1770's registers are 1 MHz (item 4).
- The keypad's matrix positions are the manual's (item 8).
- The 65C12 timing passes dp111's 1 MHz suite; the extra IRQ entry cycle, the 2 MHz build and
  a real Master's run are open (item 7).

## Not yet built

- Two coprocessors at once, with two debugger targets.
- A fitting for the modem connector (the internal SCSI board is the natural first) and a
  FRED/JIM cartridge.
- Hot plugging of cartridges.
- An inspection read of the machine's paging variables, the condition names' values.
- The configurators' recording of `at` in both clients.
- The extra IRQ entry cycle.
