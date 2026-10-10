# Acorn User Port Real Time Clock Module

## Overview

The Acorn Econet Level 3 File Server Real Time Clock Module is a small potted
unit that plugs into the BBC Micro's User Port. It provides:

1. **Real-time clock** -- the Level 3 file server reads the current date and
   time from this device to timestamp files and directories.
2. **Hardware dongle** -- the file server verifies the module's presence at
   startup by writing and reading back a known value. Without the module (or
   its emulation), the server reports "Clock failure" and refuses to start.

The module was sold by Acorn as part of the Econet Level 3 File Server package.
Its internal chip remained unidentified for roughly 38 years until the Stardot
community reverse-engineered it and identified it as a **Signetics/Philips
SAF3019P** CMOS clock/timer IC.


## The SAF3019P Chip

The SAF3019P is a CMOS real-time clock/timer in a 16-pin DIP package (SOT-38D),
manufactured by Signetics (later Philips). It is designed for battery-backed
timekeeping with a 32.768 kHz crystal oscillator.

### Capabilities

- Counts **minutes, hours, days, and months** with automatic carry and correct
  month lengths (28/29/30/31 days)
- 8 registers: 4 time counters + 4 alarm ("time") registers
- All values stored in BCD
- Battery backup (1.3V--2.6V on VSS1) for timekeeping when powered off
- Comparator output (COMP) for alarm matching
- 1 pulse/second and 1 pulse/minute outputs

### Limitations

- **No seconds counter** accessible via the serial interface (internal only)
- **No year counter** -- there is no year register at all
- **No day-of-week**
- The alarm registers (registers 1, 3, 5, 7) are freely writable, so Acorn
  repurposes them for year storage and dongle detection

### Pinout

| Pin | Name | Function                                              |
|-----|------|-------------------------------------------------------|
| 1   | NODA | Comparator mode select input                          |
| 2   | F50  | 50 Hz mains frequency input (grounded in Acorn design)|
| 3   | COMP | Comparator output (alarm)                             |
| 4   | DATA | Bidirectional serial data (CBUS, open-drain)          |
| 5   | DLEN | Data line enable input (CBUS)                         |
| 6   | CLB  | Clock burst input (CBUS)                              |
| 7   | POWF | Power failure output                                  |
| 8   | VSS2 | Ground                                                |
| 9   | MIN  | 1 pulse per minute output                             |
| 10  | SEC  | 1 pulse per second output                             |
| 11  | FSET | Frequency setting output (128 Hz)                     |
| 12  | TEST | Test mode input (must be grounded for normal operation)|
| 13  | OSCI | Crystal oscillator input                              |
| 14  | OSCO | Crystal oscillator output                             |
| 15  | VSS1 | Negative battery supply (1.5V typical)                |
| 16  | VDD  | Positive supply (+5V)                                 |


## Hardware Connections to the User Port

The module connects the SAF3019P to the BBC Micro's User VIA (6522) Port B:

| SAF3019P Pin | SAF3019P Signal | User VIA Bit | Direction (from BBC) |
|--------------|-----------------|--------------|----------------------|
| 4            | DATA            | PB0          | Bidirectional        |
| 6            | CLB             | PB1          | Output (BBC clocks)  |
| 5            | DLEN            | PB2          | Output (BBC enables) |
| 3            | COMP            | PB5          | Input (alarm status) |
| 7            | POWF            | PB7          | Input (power fail)   |

Other connections:

- Pins 13/14: 32.768 kHz crystal with 10 pF load capacitors
- Pin 15 (VSS1): 1.5V battery (SR44 silver oxide or similar)
- Pin 16 (VDD): +5V from User Port
- Pin 8 (VSS2): Ground
- Pin 12 (TEST): Grounded (critical -- floating TEST causes erratic oscillator
  behaviour, a finding from Ken Lowe's recreation project)
- Pin 2 (F50): Grounded (crystal mode, not mains frequency)


## CBUS Serial Protocol

The SAF3019P uses a proprietary 3-wire serial protocol called **CBUS** (not I2C,
not SPI):

- **DATA** (pin 4): Bidirectional, N-channel open-drain
- **DLEN** (pin 5): Data Line Enable -- HIGH indicates a transmission is active
- **CLB** (pin 6): Clock Burst -- the BBC provides clock pulses; data is
  sampled on CLB edges

### Word Formats

The CBUS defines three word types:

**TIME ADDRESS** (register selection for reading):
- 4 CLB pulses: start bit + 3 address bits (S, A0, A1)
- Followed immediately by a TIME READ

**TIME READ** (returns register data):
- 8 CLB pulses: start bit + 7 BCD data bits
- Data is driven by the SAF3019P (BBC reads PB0)

**TIME SET** (write a value to a register):
- 12 CLB pulses: start bit + 3 address bits + 7 BCD data bits + 1 bit

### Protocol Sequence (Read)

1. BBC sets DDRB to `&A7` (PB0/1/2 as outputs, PB5/7 as inputs)
2. DLEN goes HIGH (PB2 = 1)
3. BBC clocks out the 3-bit register address on DATA (PB0), toggling CLB (PB1)
4. BBC switches DDRB to `&06` (PB0 becomes input for reading)
5. BBC clocks CLB while reading DATA (PB0) for 7 bits
6. Load pulse: CLB LOW while DLEN goes LOW
7. Minimum 2 us busy period before next transmission

### Protocol Sequence (Write)

1. BBC sets DDRB to `&A7`
2. DLEN goes HIGH
3. BBC clocks out 3 address bits + 7 BCD data bits on DATA, toggling CLB
4. Load pulse and busy period as above

### Timing Parameters

| Parameter                  | Min    | Max     |
|----------------------------|--------|---------|
| CLB pulse width HIGH/LOW   | 4 us   | --      |
| Data setup (before CLB)    | 1 us   | --      |
| Data hold (after CLB)      | 2 us   | --      |
| Enable setup (DLEN before CLB) | 2 us | --    |
| CLB frequency              | 0      | 100 kHz |
| Busy time after load pulse | 2 us   | --      |


## Register Map

The SAF3019P has 8 registers addressed by 3 bits (S, A0, A1). Registers 0/2/4/6
are live time counters; registers 1/3/5/7 are alarm registers repurposed by
Acorn:

| Reg | S,A0,A1 | Datasheet Name      | BCD Range | Acorn's Use                       |
|-----|---------|---------------------|-----------|-----------------------------------|
| 0   | 0,0,0   | Month counter       | 01--12    | Current month                     |
| 1   | 1,0,0   | Month alarm register| 00--19    | Year (offset from 1981)           |
| 2   | 0,1,0   | Date counter        | 01--31    | Current day of month              |
| 3   | 1,1,0   | Date alarm register | 01--31    | Previously stored month (rollover)|
| 4   | 0,0,1   | Hours counter       | 00--23    | Current hour                      |
| 5   | 1,0,1   | Hours alarm register| 00--23    | Unused (or extended year bits)    |
| 6   | 0,1,1   | Minutes counter     | 00--59    | Current minute                    |
| 7   | 1,1,1   | Minutes alarm register| 00--59  | Dongle detection value            |

### Register Peculiarities

**Month counter (register 0):** Writing `0` does not set the month to zero.
Instead it resets the internal prescaler and seconds counter. If the seconds
counter was between 30 and 59, the minutes counter is incremented (a coarse
time correction feature).

**Month alarm register (register 1):** Only accepts values 0--19. Values 20+
are wrapped using a complex modular scheme (see the wrapping behaviour section
below). Acorn stores the year here as an offset from 1981, giving a range of
1981--2000.

**Minutes alarm register (register 7):** Used by the Level 3 file server for
dongle detection (see below).

### BCD Value Wrapping

When writing to registers, the SAF3019P wraps out-of-range values rather than
rejecting them. The wrapping rules differ by register type:

- **Month counter/alarm (regs 0, 1):** Values wrap modulo 20 with specific
  range-dependent offsets
- **Date/hour registers (regs 2--5):** Values wrap modulo 40
- **Minute registers (regs 6, 7):** Values wrap modulo 80/100

BeebEm's implementation encodes the exact wrapping behaviour observed through
testing, which follows a repeating pattern of subtract-offset-modulo operations.


## BBC Software Interface

The BBC accesses the User Port RTC by bit-banging the CBUS protocol through
OS calls:

- **OSBYTE 150** (write to User VIA register): Used to set DDRB and ORB
- **OSBYTE 151** (read from User VIA register): Used to read IRB

### DDR Configuration

```
Address phase:  DDRB = &A7  (PB0,1,2,5,7 = output; PB3,4,6 = input)
Read phase:     DDRB = &06  (PB1,2 = output; PB0 = input for reading DATA)
```

### Bit-Banging Encoding

Each bit is sent as a two-byte sequence to ORB (User VIA register &60):

| Bit Value | First Write | Second Write | Effect                    |
|-----------|-------------|--------------|---------------------------|
| 1         | &07         | &05          | DATA=1, CLB=1 then CLB=0  |
| 0         | &06         | &04          | DATA=0, CLB=1 then CLB=0  |

The end-of-transmission sequence is: `&00, &02, &00, &00`.


## Dongle Detection (Copy Protection)

The Level 3 File Server performs a hardware presence check at startup:

1. Write `&13` to register 7 (minutes alarm)
2. Read register 7 and verify it equals `&13`
3. Write `&00` to register 7
4. Read register 7 and verify it equals `&00`

If any step fails, the server displays "Clock failure" and halts. This is
effectively a hardware dongle -- without the physical module (or emulation),
the software will not run.

The Level 3 File Server source code (version 1.06 onwards) includes a
build-time `DONGLE` flag that can disable this check, presumably used by Acorn
during development. Patched versions of the file server ROM exist that bypass
the check, but emulating the hardware is the correct approach for running
unmodified software.


## Existing Emulation in Other Emulators

### BeebEm (Full Emulation)

BeebEm has complete SAF3019P emulation, implemented by Ken Lowe in 2004:

- **Files:** `Src/UserPortRTC.h`, `Src/UserPortRTC.cpp`
- **Approach:** Protocol-level emulation at the VIA port write/read level
- **Time source:** Host system clock for time counters (registers 0, 2, 4, 6)
- **Alarm registers:** Stored in a persistent array `UserPortRTCRegisters[8]`
- **Persistence:** Register state saved to `Preferences.cfg`
- **Toggle:** `Hardware > User Port RTC Module` menu option

The implementation intercepts three User VIA operations:

1. **`UserPortRTCWrite(value)`** -- called on ORB write (VIA register 0).
   Detects falling edges on PB1 (CLB). When PB2 (DLEN) is high, shifts in
   command/address bits. When DLEN goes low, either writes to a register
   (if 11 bits received) or reads a register (otherwise), using the host
   system clock for time counter values.

2. **`UserPortRTCReadBit()`** -- called on IRB read (VIA register 0). Returns
   the next bit from the shift register, LSB first.

3. **`UserPortRTCResetWrite()`** -- called on DDRB write (VIA register 2).
   Resets the bit counter when all 3 low bits are set as outputs.

### B2

No User Port RTC emulation. Only emulates the MC146818 for Master 128.

### B-Em

No User Port RTC emulation. Only emulates the HD146818 for Master 128.


## Beebium Implementation

The Acorn User Port RTC is implemented as the `acorn-rtc` peripheral extension
plugin, loaded dynamically via the extension framework.

### Architecture

```
src/extensions/acorn-rtc/
    manifest.json               # Plugin metadata + CLI parameter schema
    AcornRtcExtension.hpp/cpp   # PeripheralExtension + UserPortDevice
    Saf3019p.hpp/cpp            # SAF3019P chip emulation
    TimeParser.hpp/cpp          # CLI time/offset parsing
    acorn_rtc.proto             # gRPC service definition
    AcornRtcService.hpp         # gRPC service implementation
    plugin_entry.cpp            # BEEBIUM_PLUGIN_EXPORT entry point
    CMakeLists.txt
```

### User Port Framework

The extension framework was extended with a `UserPortDevice` interface that
exposes only the User Port signals (PB0--PB7, CB1, CB2) -- deliberately NOT
a subclass of `ViaPeripheral` to prevent extensions from accessing Port A or
CA1/CA2. The `UserPort` handle enforces a single-device constraint (the User
Port has one physical connector) and contains an internal `ViaPeripheral`
bridge that forwards only Port B and CB1/CB2.

### SAF3019P Chip Emulation

The `Saf3019p` class faithfully emulates the chip's behaviour:

- **CBUS protocol state machine:** Detects CLB edges on VIA Port B writes.
  Data is sampled on CLB falling edges during address/write phases. During
  TIME READ, data shifts on CLB **rising** edges (per datasheet Fig. 3).
- **Register wrapping:** Raw-byte wrapping per register type, matching
  BeebEm's empirical findings (month counter complex ranges, month alarm
  mod 20, date/hour ranges, minute ranges).
- **UC bit handling:** Register 0 (month counter) writes strip bit 6 (the
  UC comparator control bit) before applying wrapping.
- **Internal counter advancement:** Counters advance at wall-clock rate
  following the chip's own month-length table. February is always 28 days
  (no year awareness). The FS's `REDFIX` routine compensates for this in
  leap years.
- **Prescaler reset:** Writing BCD 0 to register 0 resets the internal
  seconds accumulator. If accumulated seconds >= 30, minutes are incremented
  (coarse time correction per datasheet).

### Time Model

Counters are self-contained after initialisation. There is no persistent
offset from the host clock. At startup, the CLI `--acorn-rtc time=...` or
`--acorn-rtc offset=...` computes a target datetime which is decomposed into
register values. After that, the chip's counters free-run at wall-clock pace.
BBC software can set the time via CBUS writes (e.g. Econet function code 28).
The gRPC service provides equivalent read/write access from the host side.

This design avoids conflicts with the FS's year rollover detection (which
compares the current month to the stored old month in register 3) and the
leap year fixup (which compensates for the chip's 28-day February). An
offset-based approach was rejected because auto-computing the year would
race with the FS's year increment, and deriving month/day from the host clock
(which knows about Feb 29) would cause spurious REDFIX corrections.

### Register Layouts

Three register layouts are supported via the `layout` CLI parameter:

**`4bit-year-in-r1`**: Original Acorn convention. Not Y2K compliant: host
time cannot be used, so give a `time` or an `offset` in range.
- Register 1: year as BCD offset from 1981 (0--15). Range: 1981--1996.
- Register 7: dongle detection.
- Register 5: unused.
- Used by: Acorn FS v0.90, v1.01--v1.07, v1.24.

**`7bit-year-in-r7`** (default): Year relocated to register 7. The default
because Beebium ships the Level 3 File Server 1.26 and the range holds host
time.
- Register 7: year as binary 2-digit value (0--99). Range: 1981--2099.
- Register 5: dongle detection.
- Register 1: unused.
- Used by: L3 FS v1.26 and similar revisions.

**`7bit-year-in-r1r5`**: BeebMaster Y2KFIX scheme.
- Register 1: low 4 bits of year offset from 1981.
- Register 5: high 3 bits of year offset from 1981.
- Year = 1981 + reg1 + (reg5 * 16). Range: 1981--2108.
- Register 7: dongle detection.
- Used by: BeebMaster Y2KFIX patched FS binaries.

The chip hardware emulation is identical for all three; only `initialise()`
and `current_datetime()` differ in which registers hold the year and how it
is encoded.

### CLI Usage

```
--acorn-rtc                              # Host local time, 7bit-year-in-r7 layout
--acorn-rtc time=1985-10-26T0121         # Specific time (compact ISO 8601)
--acorn-rtc offset=-10y                  # 10 years before host time
--acorn-rtc layout=7bit-year-in-r7             # 7-bit year layout
--acorn-rtc layout=7bit-year-in-r7:time=1985-10-26T0121
```

A start the layout cannot hold is refused before launch, by `start` and
`resolve-preset` alike (the extension's configuration check): a `time`, or an
`offset` or host time judged by the host clock when the check runs.

Note: the compact ISO 8601 format (`T0121` not `T01:21`) avoids the colon
being interpreted as an extension argument separator.

### gRPC Service

```protobuf
service AcornRtcService {
    rpc GetTime(GetRtcTimeRequest) returns (GetRtcTimeResponse);
    rpc SetTime(SetRtcTimeRequest) returns (SetRtcTimeResponse);
    rpc GetRegisters(GetRtcRegistersRequest) returns (GetRtcRegistersResponse);
    rpc SetRegister(SetRtcRegisterRequest) returns (SetRtcRegisterResponse);
}
```

`SetTime` validates the year is representable for the current layout and
returns `INVALID_ARGUMENT` if not. `SetRegister` applies chip wrapping
faithfully with no validation (allowing testing of wrapping behaviour).

### Differences from BeebEm

1. **Faithful counter advancement:** BeebEm derives time counters from
   the host clock on every read. Beebium advances counters internally
   following the SAF3019P's own calendar rules, avoiding conflicts with
   the FS's leap year fixup and year rollover detection.
2. **CLB edge polarity:** BeebEm shifts data on CLB falling edges in
   all phases. Beebium correctly shifts on CLB rising edges during
   TIME READ (per datasheet Fig. 3).
3. **UC bit:** BeebEm passes the full byte including bit 6 to the month
   counter wrapping. Beebium strips the UC bit before wrapping.
4. **Register layouts:** BeebEm supports only the original Acorn layout.
   Beebium supports both 4-bit and 7-bit year layouts.


## Year Storage Schemes

The SAF3019P has no year counter. Different software versions use different
alarm registers to store the year, creating incompatible conventions.

### 4-Bit Year (Original Acorn, v1.06--v1.24)

Year stored as a 4-bit BCD offset from 1981 in register 1 (the month alarm
register, which has a 5-bit field but Acorn uses only 4 bits). Range:
1981--1996 (offsets 0--15). The 4-bit limitation comes from the in-memory
DATE format, not the chip. See `docs/FileServer-RTC-and-Timekeeping.md` for
the full evolution from 4-bit to 7-bit year encoding.

### 7-Bit Year (L3 FS v1.26)

Year stored as a binary 2-digit value (0--99) in register 7 (the minute alarm
register, 7-bit field). The dongle detection is moved to register 5 (hours
alarm). Range: 1981--2099. Note that register 7 stores the absolute 2-digit
year (e.g. 85 for 1985), not an offset from 1981.

### BeebMaster's Y2KFIX

Stores year bits 0--3 in register 1 and bits 4--6 in register 5 (hours alarm).
The year is reconstructed as `year = 1981 + reg1 + (reg5 * 16)`, supporting
years up to 2108. Not currently supported by Beebium.

### J.G. Harston's Y2K Patch (v0.92, v1.25)

Extended the in-memory year format to 7 bits but deliberately disabled writes
to the SAF3019P (SETTME returns immediately). In v1.25, RDDONG is replaced
with OSWORD 14 to read the BBC Master's onboard RTC instead of the SAF3019P.
See `docs/FileServer-RTC-and-Timekeeping.md` for details.

### SAF3019P Register 1 Width Limitation

The SAF3019P's month alarm register (register 1) has only 5 data bits. BCD
values 0--19 (decimal 0--19) round-trip correctly. BCD 20 (0x20) requires
bit 5, which is not stored; it reads back as 0. This means year offsets
above 19 are silently corrupted. See `docs/FileServer-RTC-and-Timekeeping.md`
section "Alarm Register Bit Widths and the Year 2001 Problem."

### Date Rollover Bug

Setting the time to 23:59 on a month boundary when seconds > 30 can cause the
date to skip a day. The seconds reset operation (writing 0 to register 0)
increments the minutes counter when seconds >= 30, which cascades to
hours/date. The workaround is to call seconds reset before changing time/date
values.


## Ken Lowe's Hardware Recreation

Ken Lowe (Stardot forum user KenLowe) designed replacement PCBs for the
original Acorn module:

- Both SMD and through-hole variants
- Uses genuine SAF3019P chips (from remaining stock; the chip is discontinued)
- SR44 silver oxide battery (1.55V, expected ~2 years at 10 uA)
- Green LED for data traffic, red LED for 1 Hz clock pulse
- 32.768 kHz crystal with 10 pF load capacitors
- 90-degree User Port connector

**Critical finding:** Pin 12 (TEST) must be grounded. A floating TEST pin
causes erratic oscillator behaviour -- this was the key discovery that made the
recreation work.

**Battery voltage:** A 3V battery (e.g., CR2032) without a dropping diode
causes the oscillator to race. The SAF3019P is specified for 1.3V--2.6V battery
voltage.


## References

### Stardot Forum Threads

- [Clock Failure -- original investigation thread](https://stardot.org.uk/forums/viewtopic.php?t=20050)
  Discussion of the Level 3 File Server clock module, chip identification,
  register-level reverse engineering
- [Ken Lowe's RTC recreation (post #352389)](https://stardot.org.uk/forums/viewtopic.php?p=352389#p352389)
  PCB design, pin 12 grounding discovery, battery considerations
- [BBC Micro Real Time Clock Module](https://stardot.org.uk/forums/viewtopic.php?t=20108)
  Additional hardware discussion
- [Original RTC investigation (2014)](https://stardot.org.uk/forums/viewtopic.php?t=5164)
  Earlier community investigation

### Datasheets

- [SAF3019P Datasheet (Signetics)](http://www.elektronikjk.pl/elementy_czynne/IC/SAF3019P.pdf)
- [SAF3019P at Datasheet Archive](https://www.datasheetarchive.com/SAF3019P-datasheet.html)

### Emulator Source Code

- BeebEm: `Src/UserPortRTC.cpp`, `Src/UserPortRTC.h` (GPL, Ken Lowe 2004)
  Full protocol-level emulation of the SAF3019P via User VIA Port B
- BeebEm: `Help/rtc.html` -- user documentation for all three RTC types


## Testing

### C++ Unit Tests

- `test_saf3019p.cpp`: BCD wrapping, counter advancement (including 28-day
  February rollover), CBUS write/read protocol, dongle detection, prescaler
  reset, initialisation with leap flag. 13 test cases, 109 assertions.
- `test_saf3019p_v126.cpp`: Exact replication of the L3 v1.26 CBUS byte
  sequences (DNG05, DNG30, DNG10, DNG00) at the raw ORB level. Dongle
  detection on register 5, RDDONG/WRDONG with both register layouts.
  6 test cases, 36 assertions.
- `test_user_port.cpp`: UserPort framework (device attachment, CB1/CB2
  forwarding, Port A isolation, single-device constraint). 5 test cases.
- `test_time_parser.cpp`: ISO 8601 parsing (standard and compact formats),
  relative offsets. 5 test cases.
- `test_grpc_acorn_rtc.cpp`: gRPC service round-trip (GetTime, SetTime,
  GetRegisters, SetRegister, year range validation). 5 test cases.

### Integration Tests

- `integration_tests/acorn-rtc/`: BBC BASIC program that bit-bangs the CBUS
  protocol via OSBYTE 150/151, performs dongle detection on register 7, and
  reads all time registers. Tokenised via basictool, loaded from DFS SSD,
  run in the emulator, screen output verified by Python test.
