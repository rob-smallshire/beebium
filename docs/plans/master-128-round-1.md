# BBC Master 128: the first round (#189)

The first Beebium Master. It follows the research in
`docs/discussion/master-128-research.md`, whose section 4 holds the decisions this plan
implements and whose section 5 lists what is still to be measured on a real Master. Read the
research first; this plan says what to build, in what order, and how each step is proved.

## Outcome

A `beebium-master-128` server (model id `master-128`) that boots MOS 3.20 to BASIC IV in
Mode 7 with the clock showing the right time, reads and writes discs through its 1770, takes the
keypad, keeps its CMOS configuration across runs in the machine directory, finds a Tube
coprocessor on either the internal or the external Tube, takes cartridge ROM pairs, and runs the
Model B extensions that attach to the 1 MHz bus. A second preset boots MOS 3.50. The clients
need no change: they take model names, presets and executables from the server's catalogues
(`docs/client-specification.md`, the variant rule).

Out of this round: the modem connector's first fitting (the internal SCSI board), FRED/JIM on
the cartridge bus, the Master Compact, the Master Turbo and 512 as named presets (the internal
Tube with the existing coprocessor plugins covers the Turbo's hardware already), and the 65SC12
timing oracle unless beebjit's 65C12 suite drops in cleanly (step 9).

## Names

- Executable `beebium-master-128`, model id `master-128`, policy `Master128Hardware`, machine
  alias `Master128` in `Machines.hpp`, main `src/server/main_master_128.cpp`. The B's variants
  carry "model-b" because the machine is the BBC Model B; this one is the BBC Master 128.
- Presets `master-128` (MOS 3.20, the default for the model) and `master-128-mos-3-50`.
- Battery-backed unit `master-128-cmos` (the clock and the CMOS RAM are one chip, one unit),
  hardware descriptor "Master 128: MC146818".
- Attachment points: `tube` stays the external Tube so existing presets and the coprocessor
  plugins keep working; `internal-tube` is the new one; `1mhz-bus` stays the external 1 MHz
  bus; `cartridge-rear` (ROMs 0 and 1, SK3) and `cartridge-front` (ROMs 2 and 3, SK4);
  `modem-connector` declared with no fitting yet.
- Debugger variables: `accd`, `acce`, `accx`, `accy`, `accitu`, `accifj`, `acctst`, `accirr`,
  `andy`, `bank`, `romsel`, `shadow_access` (posted on #189).

## Steps

Each step is one or more commits on master with its own targeted tests; the full suites run
before a push, as always. Steps 1 to 3 are sequential; 4 to 8 are independent of each other once
3 is in; 9 and 10 close the round.

### 1. The policy, the paging and the clock: boot to BASIC

`Master128Hardware` with the memory map and the two paging registers, the System VIA's clock
wiring, the CMOS seed and its battery unit. This is the smallest machine that boots, because the
MOS reads LANG and FILE from CMOS before it can show a prompt.

- CPU: the existing `Cmos65C02` policy (65SC12 instruction set). Clock: `BusStretching` with the
  Master's map, &FE00-&FE1F, &FE28-&FE2B and &FE40-&FE9F stretched (beebjit's and B2's table);
  FRED and JIM stretched while IFJ is clear and not while it is set.
- RAM: 32K main; LYNNE 20K at &3000-&7FFF; HAZEL 8K at &C000-&DFFF; ANDY 4K at &8000-&8FFF;
  sideways banks 4 to 7 RAM.
- ROMSEL &FE30-&FE33: bits 0-3 bank, bit 7 ANDY; readable, with bits 4-6 returning what was
  written until section 5 item 2 says otherwise (note the assumption in a comment).
- ACCCON &FE34-&FE37: all eight bits stored and readable. D routes the display to LYNNE
  (switching at the character where the write lands; with D set, screen addresses below &3000
  display ANDY and HAZEL, as beebjit verified). X sends every CPU access to &3000-&7FFF to
  LYNNE. E applies the SYNC rule: an access goes to LYNNE when the previous cycle was an opcode
  fetch from &C000-&DFFF and this cycle is not an opcode fetch; opcode fetches are never
  redirected. Y pages HAZEL over &C000-&DFFF. Y's effect on E is unmeasured: implement E
  independently of Y (Acorn's description) and leave a named constant to flip, with the
  question in section 5. IFJ, ITU, TST and IRR are stored here and consumed in steps 6, 5, 1
  and 1: TST set makes reads of &FC00-&FFFF return the MOS ROM (the emulators' consensus,
  unverified; comment it); IRR set asserts a level IRQ through the aggregator.
- Stale data bus: reads of unmapped SHEILA (&FE2C-&FE2F if undecoded, &FEC0-&FEDF, the 1770
  latch at &FE24, the modem select with nothing fitted, the Tube with nothing fitted) return
  the last byte the video system fetched. Put the byte where the policy can read it; the B+
  already keeps a video fetch path.
- The MOS: `acorn-mos_3_20.rom` as the default MOS image; banks 9 to 15 from
  `acorn-dfs_2_24.rom`, `acornsoft-viewsheet_b1_0.rom`, `acorn-edit_1_00.rom`, `bbc-basic_4.rom`,
  `acorn-adfs_1_50.rom`, `acornsoft-view_b3_0.rom`, `acorn-terminal_1_20_mos_3_20.rom`, locked
  (they are inside the MOS chip). Bank 8 is the IC27 socket, empty by default. Banks 0 to 3
  are the cartridge slots (step 6); until then, empty. The 3.50 set is the same shape with the
  `_3_50` files (step 8 makes it a preset).
- System VIA: a Master variant of `SystemViaPeripheral`. Port A carries the clock's data when
  the clock is selected; PB6 is CE and PB7 AS as outputs; addressable-latch bits 1 and 2 are
  R/W and DS (the B's speech read and write). Follow the datasheet
  (`docs/datasheets/MC146818.md`, bus section): the address latches on the falling edge of AS
  with CE low; a read drives port A while DS is high with R/W high; a write latches port A on
  the falling edge of DS with R/W low. No speech chip: the latch bits do nothing else.
- Clock: `Mc146818Rtc` ticked from the policy, host time plus offset, `reset()` on Break (the
  chip's RESET pin is on the system reset per the circuit diagram). Battery unit:
  `Mc146818BatteryUnit` named `master-128-cmos` with the Welcome Guide seed (section 4 item 3,
  with FLOPPY and FILE 13, encoded per the CMOS byte map in section 1.4; the
  station byte 0 from `--station` when given, else 0). R held at power-on is the MOS's own code zeroing CMOS through the VIA: nothing to
  write, one test to prove it.
- The one-cycle-longer IRQ entry of the Master is a CPU-library question; leave it, note it.
- Server: `Machines.hpp` alias, `MachineModels.hpp` entry, `main_master_128.cpp`,
  `add_beebium_server`, the system preset `master-128` with the 1770 fitted (step 3 adds the
  controller; the preset can precede it with no drive).

Tests: a policy unit test for each paging rule (ROMSEL and ACCCON readback; X; E with the SYNC
rule at both edges of &C000-&DFFF and across an opcode fetch; D display routing including
below &3000; Y; TST; IRR); the stale bus; the VIA clock sequence as the MOS performs it (the
sequence is in section 1.4) reading and writing a CMOS byte; the seed's bytes; a boot test
through screen text that MOS 3.20 reaches `BASIC` and `>` in Mode 7 with the Welcome Guide
settings; `*TIME` showing the host's time; `*CONFIGURE MODE 0` then Ctrl-Break coming up in
Mode 0, and the same after Save, shutdown and relaunch of the machine directory; R held at
power-on producing "CMOS RAM reset". Also the Planet Nubium corner (X = 1, E = 0: the VDU
driver's writes land in LYNNE) as a unit test.

### 2. The keyboard

`KeyboardMatrix` grows to 13 columns; the keypad's 19 keys at the internal numbers in section
1.5; names in the key catalogue (`KeyboardMapping`) and the mapping file vocabulary
(`docs/keyboard-mapping-files.md`): `Keypad0` to `Keypad9`, `KeypadPlus`, `KeypadMinus`,
`KeypadDivide`, `KeypadHash`, `KeypadMultiply`, `KeypadComma`, `KeypadPeriod`, `KeypadReturn`,
`KeypadDelete`. The built-in mappings map the host's numeric keypad to them (W3C `Numpad*`
codes) without renaming anything that exists. No start-up links: the B's link handling is not
instantiated. The auto-scan already wraps over 16 columns.

Tests: matrix tests for the new columns; a mapping-file conformance case; a boot test typing a
digit on the keypad and seeing it on screen.

### 3. The disc controller

`Acorn1770DiscController` gains the Master latch layout: latch at &FE24-&FE27 (DS0 bit 0, DS1
bit 1, RES bit 2, DS2 bit 3, SEL side bit 4, DDEN bit 5), registers at &FE28-&FE2B; the latch
reads as the stale bus. No 8271 on this model (`--fdc` is not offered). DS2 is a third drive
select: model it as a select with no drive unless a preset says otherwise. The B+ comments that
describe the latch as the only 1770 layout are updated.

Tests: a controller test of the Master latch bits; `*CAT` of a DFS disc and of an ADFS disc
through screen text; a DFS format and save round trip (`docs/disc-subsystem.md`'s matrix).

### 4. The small moves

ADC at &FE18-&FE1F (`AnaloguePortDevice` at the new address; &FEC0 unmapped). Econet INTOFF and
INTON at &FE38 and &FE3C; the station number read from CMOS byte 0 by the policy for the
socket, so `--station` and `*CONFIGURE` agree (the station register at &FE18 is gone). The
Video ULA decodes &FE20-&FE23 only; no Mode 7 alias at &3C00.

Tests: the existing analogue port and Econet socket tests re-pointed at the Master addresses;
an Econet status test that `--station 42` shows station 42 and CMOS byte 0 = 42.

### 5. Two Tubes

Two `TubeSocket` instances, or one socket behind a selector, as the attachment-point framework
prefers; the attachment ids `tube` (external, PL6) and `internal-tube` (SK1/SK2). ITU steers
&FEE0-&FEE7 and the host-side Tube IRQ to one of them; the other coprocessor keeps running
unreached (B2's model). The MOS probes the configured side then toggles ITU: with INTUBE in
CMOS and a coprocessor on `tube`, the MOS must still find it. The Tube contract's clock: the
coprocessor sees a steady 2 MHz Phi2.

Tests: the Tube subsystem tests run on the Master for both attachment points; a preset with the
65C102 plugin on `internal-tube` boots to the coprocessor's banner (a Master Turbo in all but
name); the cross-probe case (INTUBE configured, coprocessor external); `accitu` visible to the
debugger.

### 6. The 1 MHz bus, the cartridge slots and the modem connector

`OneMHzBusPort` stays the external bus (`1mhz-bus`, PL5); the SCSI and Music 5000 extensions
attach there unchanged, at 1 MHz while IFJ is clear. A second port, the cartridge bus, is
declared but takes no FRED/JIM fittings in this round; IFJ set routes &FC00-&FDFF to it at
2 MHz, so with nothing on it reads return the stale bus and the external bus is not reached.
The cartridge slots `cartridge-rear` (banks 0 and 1) and `cartridge-front` (banks 2 and 3)
are one-of attachment points (the `at` placement of step 5): a cartridge is an extension
instance that provides the slot's sideways banks (Rob's framing: cartridge extensions provide
sideways slots, so the port is a provider of two banks for reads and, later, writes, and a
FRED/JIM cartridge adds bus handlers on the same instance), and the first fitting is a built-in
ROM cartridge with two 16K sockets, the even-bank and odd-bank images, each optional but at
least one given; a socket left empty leaves its bank empty, exactly as a one-chip cartridge
does on Acorn's board (Rob: be honest to the real cartridge); its port fills the slot's banks,
which SidewaysService reports read-only with the images' names. Insertion and removal
at run time wait for hot plugging in the framework; a swap is a relaunch, as Acorn told owners
to power off before changing a cartridge. This is the shape a FRED/JIM cartridge needs, so the
slot and the bus device stay one thing. `docs/sideways-slots.md`'s reservation of
`runtime_configurable = true` for cartridges is superseded and is amended with the commit. The IC41 and IC37
sockets with LK18 and LK19 (32K ROM pairs replacing sideways RAM banks 4/5 and 6/7) are
motherboard links in the existing links mechanism, West by default. The `modem-connector`
attachment point is declared, decoding nMODEM at &FE80-&FE9F at 1 MHz on the internal bus
(A0-A3, so the 32 bytes are 16 registers mirrored once), with no fitting; a read with nothing
fitted is the stale bus.

Tests: a ROM cartridge placed at each slot appears at its banks, a one-chip one on its own bank
with the other bank empty, is read-only
to the CPU and to SidewaysService (SetSlot refused with a sentence), and the Memory view names
its images; a cartridge at a point a Model B lacks is refused; IFJ routing
(an extension on the external bus is unreachable while IFJ is set); the modem select's range
on an unfitted machine.

### 7. The debugger and inspection

The variables in the Names section through the policy's `debug_variables`, with
`shadow_access` evaluated after the X and E rules for the access under test;
`HostHasPcAwareMemory` as the B+ does it. `DeviceInspection` exposes ACCCON and ROMSEL with
the clock registers (the RTC inspection already keys on a `rtc` member).

Tests: the paging-qualified breakpoint tests extended to the Master names; an inspection test.

### 8. Presets, packaging and the lists

The `master-128-mos-3-50` preset (the `_3_50` ROM files in banks 9 to 15 and
`acorn-mos_3_50.rom`). Every list a new variant must join: the CI artifact paths, the Scoop
manifest, `packaging/python-server/pyproject.toml` (which lists four of the seven existing
variants; bring it to all eight), the Python client's variant list and the installation
README, `scripts/smoke-installed-tree.sh`, the install smoke tests, `CLAUDE.md` (process
model, build targets, hardware policies, current state) and `docs/deployment.md`'s default ROM
table. The TypeScript client locates only `beebium-model-b`; leave it, it is a client matter.

Tests: `describe-preset-schema` lists the model and both presets; the installed-tree smoke
runs the new executable.

### 9. The timing oracle

beebjit carries a 65C12 build of dp111's timing suite (`65C12timing1M.ssd`, made by its
`make_master_rom.c`). If it drops into `tests/assets/6502timing/` under the existing asset rules
with a Catch2 wrapper like the B's, add it as the Master's slow-tier timing lane and record the
first results in the research document's section 5 item 7. If it does not drop in, file an
issue and move on; the round does not wait on it.

### 10. As-built documentation

`docs/master-128.md`: the machine as built, in the shape of `docs/cassette.md`: the memory map
and paging rules, the SHEILA map, the clock and CMOS (seed, unit, `*CONFIGURE` survival,
R-reset), the keypad, the 1770, the two Tubes, the buses and cartridges, presets, the debugger
vocabulary, the testing table, and what is simplified or unmeasured (pointing at section 5).
`CLAUDE.md` gets its entry. `docs/sideways-slots.md` gets the Master's topology. The research
document's section 5 is updated with whatever the round measured on hardware.

## Acceptance

Release build. Launch `beebium-master-128` from the macOS and Linux clients with no client
change; Mode 7 BASIC prompt with the right time from `*TIME`; `*CONFIGURE` survives Save and
relaunch; a DFS disc and an ADFS disc catalogue and load; a Tube coprocessor on each Tube;
the Music 5000 preset plays on the external bus; the Stratagem project's Master build runs
(its author tests on a Master Turbo, so the `internal-tube` 65C102 preset is the one to hand
them). The research document's section 5 measurements that Rob has made are reflected in the
code or recorded as still open.
