# Cassette interface: research findings (#208, round 1)

Research for the cassette specification. It is a survey, not a design: it records what the documents say, what the reference emulators do, and what nobody knows, so that the specification can choose with its eyes open. No code was written or changed.

**Scope.** The requirements go beyond #208 as filed:
- loading from UEF;
- if feasible in real time, loading from a WAV file and from live audio played into an audio input.

So the analogue side is in scope. The Serial ULA's demodulator becomes a real component, and section 7 gathers what that needs. Where sections 1-6 and section 7 overlap, section 7.1 carries the fuller evidence and the parameter table.

Every answer cites its source. Where a forum thread is the source it is summarised, not quoted. The Serial ULA is undocumented custom silicon; points no document settles are marked **[undocumented]**, and inferences are marked **[inferred]**.

## Sources and abbreviations

| Tag | Source |
|---|---|
| AUG | `docs/manuals_text/Advanced_User_Guide/` (printed page numbers as they appear in the transcript) |
| UG | `docs/manuals_text/BBC_User_Guide/` |
| NAUG | `docs/manuals_text/BBC_Master-New-Advanced-User-Guide/` |
| SM-B | `docs/datasheets/Acorn_BBCSMOct85_Sec1.pdf`, Model A/B service manual, Oct 1985 (PDF page / printed page) |
| SM-B+ | `docs/datasheets/Acorn_BBC_Service_Manual_Oct85_Sec2_Model_B_Plus.pdf` (PDF page / printed page) |
| 6850 | `docs/datasheets/MC6850_ACIA.pdf` (Motorola data book pages 4-527 to 4-535) |
| DIS | `~/Code/os120/os120_acme.a`, an annotated reassembly of MOS 1.20 (line numbers) |
| SRC | `~/Code/AcornOS120/src/MOS70`..`MOS76`, Acorn's original MOS 1.20 cassette source (CR line endings; line numbers after converting to LF) |
| T22935 | stardot, "The Beeb's cassette storage input circuit", https://stardot.org.uk/forums/viewtopic.php?f=3&t=22935 (scope measurements of a Ferranti ULA, 2021) |
| P326998 | stardot, beebjit 0.9.6 release thread, DCD threshold measurements, https://stardot.org.uk/forums/viewtopic.php?p=326998#p326998 |
| BeebWiki | "Serial ULA", https://beebwiki.mdfs.net/Serial_ULA |
| b2 | `~/Code/b2` (master, plus the upstream `wip/tom` branch) |
| BeebEm | `~/Code/beebem-windows` (line numbers); `~/Code/beebem-mac` has the same tape logic |
| B-Em | `~/Code/b-em` |
| beebjit | `~/Code/beebjit` |
| jsbeeb | `~/Code/jsbeeb` |
| MAME | `~/Code/mame/src/mame/acorn/acorn_serproc.cpp` and `bbc*.cpp` |

---

## 1. The hardware

### 1.1 Parts and clock

- **The chips.** Cassette and RS423 share one MC6850 ACIA at &FE08/&FE09. A Ferranti ULA, Acorn's "serial processor", sits at &FE10 (AUG ch.20 introduction and §20.9, pp.385, 392; SM-B §3.5, PDF p.29).
  - On the Model B the ULA is IC7, part 2C199, with variants 2C199E, -E-3 and -E-7 (SM-B IC table, PDF p.66; fault finding, PDF p.51).
  - On the B+ it is IC85 "SERPROC", and the ACIA is IC82 (SM-B+ §5.12, PDF p.30).
  - The later second source is the VLSI VC2026 (MAME header; BeebWiki).
- **What Acorn says the ULA contains:**
  - two baud-rate generators, one for transmit and one for receive;
  - a cassette data and clock separator;
  - the RS423/cassette switching;
  - a sine synthesiser for the record signal;
  - a high-tone detector that drives the ACIA's /DCD.

  Sources: SM-B §2 (PDF p.17), §3.5 (PDF p.29); AUG §20.9; SM-B+ IC85 description (PDF p.91).
- **ULA clock.** 16 MHz / 13 = 1.2308 MHz (SM-B §3.5, PDF p.29; fault finding, PDF p.51). Every rate it makes is about 0.16% above nominal: 19231 Hz rather than 19200 Hz (BeebWiki).
- **Coherent tones.** 1.2308 MHz / 1024 = 1201.9 Hz is the "1200 Hz" tone, and /512 = 2403.8 Hz is the "2400 Hz" tone (SM-B §3.5, PDF p.29). The 300-baud ULA clock of 19231 Hz divided by 16 gives 1201.9 baud. Tone and bit clock therefore come from one divider chain, and at 1200 baud one bit lasts exactly one 1200 Hz cycle (SM-B+ §5.12, PDF p.30).

### 1.2 The &FE10 control register

Sources: AUG §20.9.1-20.9.4, pp.392-393; NAUG §15.3.12, pp.247-248; SM-B+ §5.12, PDF p.31.

| Bits | Function |
|---|---|
| 0-2 | Transmit rate: 000 = 19200, 100 = 9600, 010 = 4800, 110 = 2400, 001 = 1200, 101 = 300, 011 = 150, 111 = 75 baud (rates assume the ACIA divides by 64) |
| 3-5 | Receive rate, same encoding |
| 6 | 0 = cassette, 1 = RS423 |
| 7 | 1 = cassette motor relay **and** the cassette motor LED on (AUG §20.9.4) |

- **Power-on value.** &64: RS423 selected, 9600/9600, motor off (BeebWiki; MAME `device_start`; DIS 9737).
- **Reading &FE10.** A read acts as a write of &FE, which turns the motor on and selects cassette.
  - The explanation offered is that the ULA has no R/W line, so on a read cycle the data bus still holds the address high byte, &FE.
  - beebjit (`serial_ula.c:154-166`), jsbeeb (`serial.js:27-30`) and MAME (`read()`) all model it.
  - **[undocumented]** in Acorn literature.
  - Beebium's `SerialUla::read` returns &FF and has no side effect (`SerialUla.hpp:89-92`).

### 1.3 The ACIA on the cassette side: clocks and divide ratios

- **In cassette mode the ULA field does not set the tape speed; the ACIA divider does.**
  - The MOS fixes the ULA at the "300 baud" setting (19.23 kHz, i.e. 64 × 300).
  - The ACIA's counter divide then selects the speed: /16 for 1200 baud, /64 for 300 baud.
  - /1 would be 19200 baud, which the tape path cannot carry.

  Sources: AUG §20.6.1 (p.388), where the data separator needs the ULA at 300 baud and 1200 baud comes from changing the divide to 16; AUG §14.2.5 (p.314); NAUG §15.3.7 (p.244), the counter "is used to set the cassette system baud rates".
- **RS423 is the other way round.** It normally keeps the ACIA at /64 and picks the speed with the ULA fields (AUG §20.6.1; NAUG p.244).
- **On playback the receive clock is recovered from the tape signal.** SM-B+ §5.12 (PDF p.30) says the cassette receive bit rate is "derived from the FSK signal not from the serial processor control register bits". The IC85 description (PDF p.91) says a bit clock is recovered by demodulation.
  - BeebWiki adds that bits 3-5 are ignored entirely in cassette mode.
  - b2's notes quote the same (`doc/b2_notes.org`, around line 5054).
- **Typical ACIA control values:**
  - AUG §20.6 (p.388) gives &56 as the idle cassette value: /64, 8N1, RTS high.
  - A scope capture of a 1200-baud load saw &D5: /16, 8N1, RTS high, receive IRQ on (T22935).
  - Section 2.1 tabulates what MOS 1.20 actually writes.
- **6850 receiver behaviour in /16 and /64 modes.** The receiver detects a start bit by sampling low for 8 or 32 clocks, and resynchronises on each start bit (6850 p.4-532).

**Consequence for Beebium.** `docs/serial-acia.md` and `SerialUla` derive the receive bit period as `CPU_HZ / baud` from the ULA's receive field. `Mc6850` treats the counter divide as informational, citing b2's "REMOVED CLOCK DIVISION" (`Mc6850.hpp:33-38`).

- For cassette both choices are wrong: the field is fixed at 300, and only the divider distinguishes 300 baud from 1200.
- The `Mc6850` comment is also out of date. Current b2 master applies the /1, /16 and /64 masks inside its 6850 (`MC6850.cpp:101-117`, 180ff).

### 1.4 Encoding on tape

- **The standard.** CUTS (Kansas City derived): 1200 Hz and 2400 Hz at 300 or 1200 baud (SM-B §1.2.1, PDF p.11; SM-B+ §3.5, PDF p.12).
- **Tone per bit.** Logic 0 is 1200 Hz. Logic 1 is 2400 Hz, which is also the "carrier" or "high tone" (AUG §14.2.4, p.312; §13.8, p.300).
- **Cycles per bit:**

  | Baud | Bit period | 0 | 1 |
  |---|---|---|---|
  | 1200 | 833 µs | 1 cycle at 1200 Hz | 2 cycles at 2400 Hz |
  | 300 | 3.33 ms | 4 cycles at 1200 Hz | 8 cycles at 2400 Hz |

  - The 1200-baud row is stated in T22935, and MAME synthesises it the same way.
  - **[inferred]** The 300-baud row follows from the ULA emitting whole cycles continuously while the ACIA holds TxD for four times as long (BeebWiki). The UEF specification defines the same two encodings.
- **Framing.** 8 data bits, no parity, 1 stop bit, least significant bit first. The idle (mark) line is 1, so leader is continuous 2400 Hz.

### 1.5 Playback: demodulation into RxD and RxC

- **Analogue front end.** Three LM324 stages filter and square the input: IC35 on the Model B, IC89 on the B+ (SM-B §3.5; SM-B+ PDF pp.31, 92).
  - The circuit works out to a 2nd-order high-pass at about 1.3 kHz, a 2nd-order low-pass at about 4.1 kHz, then a high-gain clipper (T22935).
  - So the ULA sees only the sign of the waveform: a square wave.
- **The data separator is [undocumented].** Scope measurements of a Ferranti part give the best public evidence (T22935):
  - RxC (ULA pin 18) comes as bursts of 4 pulses, giving 16 per 1200-baud bit to match ACIA /16.
  - With 2400 Hz input, a burst follows each signal transition about 20 µs later.
  - With 1200 Hz input, an extra burst is inserted when no transition arrives within about 260 µs. That is 320 periods of the 1.23 MHz clock, between a 2400 Hz half-cycle (208 µs) and a 1200 Hz half-cycle (416 µs).
  - Burst spacing tracks the input frequency in 3.25 µs steps.
- **[inferred] The separator is edge-driven.**
  - Every half-cycle, or every missing-edge timeout, yields 4 RxC pulses.
  - RxD carries the decoded tone level.
  - The ACIA's divider then samples one bit in 16 or 64 pulses.
  - Whether a 300-baud recording yields 4 pulses per cycle exactly as 1200 does was raised in T22935 and left open.
- **What emulators do instead.**
  - MAME times zero-crossings at 44.1 kHz and emits each recovered bit followed by 16 RxC pulses, so the ACIA divider does the work. Its comment calls the burst a fake (`receive_clock()`).
  - beebjit feeds the ACIA one bit per 1200-baud bit time, and the ACIA keeps 1 bit in 4 at /64 (`mc6850.c:266-277`). That is the proven bit-level simplification.

### 1.6 Carrier detect and /DCD

**What Acorn documents.**

- AUG §14.2.5 (p.313): the ULA's carrier detection finds inter-block gaps, and a block start is recognised only as the first thing after a carrier detect. After a bad block start the MOS "blips" the motor to break the carrier, so that a fresh carrier-detect interrupt follows when 2400 Hz is next detected.
- AUG §13.8 (p.300) describes the DCD interrupt as the tone *stopping* at the end of a block. AUG §20.7.3 (p.389) says DCD high means no carrier. These two passages conflict with §14.2.5 and with the MOS code (section 2.2).
- **The delay comes from a resistor and capacitor on ULA pin 15.**
  - Model B: R75 and C28 time the delay from run-in tone to DCD (SM-B §3.5, PDF p.30). R75 settled at 82 kΩ "to avoid loosing the first bit of the first byte of the first block" (SM-B §5.3.3, PDF p.47). By ULA variant it is 100 kΩ, 56 kΩ or 82 kΩ (fault finding, PDF p.51).
  - B+: R66 (82 kΩ) and C30 (4.7 µF), "used to time the preamble tone" (SM-B+ PDF pp.31, 70-71, 91). The time constant is about 0.39 s.
  - T22935 reports a diode tying the RC to the motor drive, so **motor off discharges the capacitor**. That is the hardware reason a motor blip re-arms the detector.
  - The Ferranti detector charges only on tones at or above about 1790 Hz (T22935).

**Measured behaviour.** These measurements are on real hardware but come from community sources, not Acorn.

- **DCD is a short pulse at tone onset, not a level.** It rises after continuous high tone has lasted the threshold, then falls about 210 µs later although the tone continues. The 6850 latches the edge in status bit 2 and raises an IRQ if CR7 is set.
  - Sources: beebjit `serial_ula.c:215-245`, citing P326998 and T22935.
  - P326998 also reports a status sequence of 2 → 6 → 2 at the threshold.
- **Threshold:**

  | Source | Measured | Notes |
  |---|---|---|
  | Ferranti 2C199E-7, issue 4 board | 0.215 s fires, 0.214 s does not | P326998 |
  | Second Ferranti part, issue 7 board | 0.200 s | P326998 |
  | Issue 3 Model B | about 0.174 s (209 bits) | jsbeeb `acia.js:149-167` |
  | VLSI parts | about 90 ms, timed internally (pin 15 sits at 5 V on a VC2026) | Second-hand, unverified |

  beebjit and jsbeeb quote a service-manual sentence giving 0.1 to 0.4 s. **That sentence is not in either local service manual PDF**, so it may come from another issue or section.
- **How fast DCD "drops" in a gap.** Because DCD is a pulse, the real question is when the detector *re-arms*. No Acorn document says; section 7.1 has the measurements.
  - Motor off re-arms it, and is required to.
  - On a Ferranti part one cycle of low tone discharges about 90% of the RC.
  - Register writes that keep the motor on do not re-arm it.
  - How silence with the motor running behaves on a Ferranti part is unmeasured. The replica's counter watches the recovered data level, so silence after high tone does not clear it.
  - Emulators re-arm on any 0 bit or silence: beebjit, jsbeeb, and BeebEm through its carrier state.
- **6850 DCD semantics** (6850 pp.4-532 to 4-534):
  - A low-to-high transition on /DCD sets status bit 2 and raises an IRQ if CR7 = 1.
  - The bit stays set until the status register **and then** the data register are read.
  - /DCD high also holds the receiver in reset and makes RDRF read empty.
  - The receive clock must be running for /DCD to be processed.
- **How the careful emulators implement it.**
  - beebjit and jsbeeb clear the latched bit on a data-register read alone. beebjit says real-hardware measurements contradict the datasheet's status-then-data sequence (`mc6850.c:114-178`).
  - beebjit leaves "/DCD high resets the receiver" as a TODO (`mc6850.c:128-131`).
- **Beebium today.**
  - `SerialUla::write` holds /DCD high whenever cassette is selected, so the receiver idles (`SerialUla.hpp:84-86`).
  - `Mc6850` latches an IRQ on the /DCD 0→1 edge, but its status bit 2 simply follows the pin (`Mc6850.hpp:186-198`, 388). There is no "held until data read" latch, and a 210 µs blip would be invisible to a status poll that came later.
- **/DCD and /CTS in RS423 mode.** /DCD is forced low whenever RS423 is selected (AUG §20.7.3). /CTS is forced low whenever cassette is selected (AUG §20.7.4, p.390).

### 1.7 Record path

- **Carrier is gated by RTS.** With the ACIA's RTS low, the ULA outputs 2400 Hz for mark and 1200 Hz for space, following TxD. With RTS high the output is silent, which the AUG calls "carrier absent".
  - Sources: AUG §14.2.4 (pp.311-312) and §20.1 (p.385), where RTS "acts as a data carrier enable for the cassette"; MAME `write_rtsi`.
  - So leader is RTS low with the ACIA idle.
- **Transmit clock.** The ULA transmit generator is set to 300 (19.23 kHz), and the ACIA's /16 or /64 then makes 1200 or 300 baud.
- **Output level.** Nominally 200 mV peak to peak at the socket (SM-B+ §3.5, PDF p.12). Irrelevant to a digital model.
- **Tone phase against bit boundaries: [undocumented].**
  - AUG §20.10 (p.393) records that changing the ACIA divide can put the ULA "out of step" for a few bits and corrupt the first character. MOS 1.0 onwards works round this by sending a dummy byte early in the leader.
  - **[inferred]** The ULA has a free-running tone and bit counter that the ACIA divider change does not reset.

### 1.8 Motor relay and LED

- **Relay.** Bit 7 drives a relay through a transistor: Q3 on the Model B, Q7/RL1 on the B+ (SM-B §3.5; SM-B+ §5.12, PDF pp.30, 71, 91). The contacts appear on DIN pins 6 and 7, rated 24 V DC at 1 A (SM-B PDF p.62; SM-B+ §3.5, PDF p.12).
- **LED.** The cassette motor LED is driven with the relay (AUG §20.9.4), on keyboard connector pin 13 (AUG Appendix J; SM-B+ PDF p.68). It is not on the addressable latch, which carries only Caps Lock and Shift Lock (AUG §23.2, p.419).
  - **[inferred]** It lights whenever bit 7 is set, whether or not a recorder is connected.

### 1.9 Connector (7-pin DIN, SK5)

SM-B+ connector pinouts, PDF p.64; UG External connections, p.499.

| Pin | Signal |
|---|---|
| 1 | Output (record) |
| 2 | 0 V |
| 3 | Input (playback) |
| 4 | Output |
| 5 | Not connected |
| 6, 7 | Motor relay contacts |

- Input impedance is over 100 kΩ, with a nominal range of 50 mV to 5 V peak to peak (SM-B+ §3.5).
- None of this matters for emulation except the motor contacts, which do not reach the machine.

### 1.10 Speed links (Model B)

Links S28/S29 select an alternative cassette and serial clock, and the documents describe it inconsistently:

- AUG Appendix I (p.486) calls it "1200 baud rate select";
- SM-B §6 (PDF p.43) calls it a "1300 baud cassette rate";
- SM-B §7.3 (PDF p.65) gives a 44% speed-up.

Emulating the default only is the obvious course.

### 1.11 What is unknowable from the documents, and how emulators answer it

| Point | Status | b2 | BeebEm | B-Em | beebjit | jsbeeb | MAME |
|---|---|---|---|---|---|---|---|
| Data separator algorithm | undocumented; scope evidence in T22935 | none | bytes injected | bytes injected | bit per 1200-baud bit time; ACIA keeps 1 in 4 at /64 | bytes injected | zero-crossing timing + 16 RxC per bit |
| Receive clock in cassette mode | SM-B+ says recovered from tape | uses ULA rx field (wrong) | ignored | ignored | fixed 1200 bit/s | ignored | from tape |
| DCD threshold | RC-dependent, 0.17-0.215 s measured (Ferranti) | none | 200 ms | none (immediate) | 240 bits = 0.2 s | 209 bits = 0.174 s | >100 bits ≈ 83 ms |
| DCD shape | pulse of about 210 µs (measured) | static | 200 µs pulse | level for the tone | one-bit (833 µs) pulse | one-bit pulse | level |
| Detector re-arm in a gap | undocumented; motor off re-arms | n/a | on leaving tone | n/a | any 0 bit or silence | any non-carrier | 0 bit or long pulse |
| &FE10 read side effect | undocumented | — | — | — | write &FE | write &FE | write &FE |
| Tone phase on record | undocumented | — | — | — | — | — | each bit starts on a positive half-cycle |
| VLSI tone-swap bit 3 | BeebWiki only | — | — | — | — | — | — |

---

## 2. MOS 1.20 cassette filing system (test vector only)

Beebium must not depend on any MOS. This section exists so that tests know what a stock MOS 1.20 will do to the hardware.

### 2.1 What the MOS writes

The MOS keeps copies of both registers: &0282 for the ULA (OSBYTE &F2, default &64) and &0250 for the ACIA (OSBYTE &C0, default &56) (DIS 9660, 9737). `.tapeBaudRate` at &C6 is 5 for 1200 baud and 6 for 300 baud, and is ORed into every ACIA write: 5 gives /16 8N1, 6 gives /64 8N1 (DIS 898-900, 19777-19784). Both speeds are therefore 8N1. Acorn's source names the constants (SRC MOS70 44-55):

| Name | Value | Meaning |
|---|---|---|
| `TXSTAT` | &30 | RTS low, transmit IRQ on |
| `RXSTAT` | &D0 | RTS high, receive IRQ on |
| `SERON` | &85 | ULA: motor on, cassette, transmit 300 |
| `SEROFF` | &05 | ULA: motor off, cassette |

| Operation | &FE10 | &FE08 | Source |
|---|---|---|---|
| `*TAPE`, `*TAPE12`, `*TAPE3` (OSBYTE &8C) | nothing | nothing | Sets &C6, closes files, resets vectors (DIS 17140-17196) |
| Claim the serial system (start of LOAD, SAVE, *CAT, OPENIN/OPENOUT) | — | &03, master reset | Waits for RS423 idle; routes ACIA IRQs to the tape code (DIS 19704-19735) |
| Read setup at **every** block header, and after every failed sync | &05, about 1 ms delay, &85: the motor blip | &D5 (1200) / &D6 (300) | DIS 19754-19784; SRC MOS76 452-471. The source says the blip bypasses the OS so the user is not bothered by it |
| SAVE: at the RECORD-then-RETURN prompt, and again after RETURN | motor on via OSBYTE, then &85 | &03, &15/&16 (RTS low so carrier is on, no IRQs), then a polled dummy **&AA** | DIS 19254, 19965-19987, 17636-17656; SRC MOS76 570-600; AUG §20.10 |
| SAVE: each block | — | &35/&36 (transmit IRQ, RTS low), then &03 after it | DIS 19744, 19027-19030 |
| End, error or Escape | copy with motor off (normally &64), written twice | &03, then the OS copy (&56) | DIS 19658-19678 |

- **Master reset leaves RTS low.** Master reset (&03) also loads CR6:5 = 00, so on SAVE the carrier keeps sounding through the inter-block delays.
- **Motor timeline.**
  - LOAD and `*CAT`: the motor comes on at the first header read and blips off for about 1 ms at every header read. It goes off at the end, on an error abort, or on Escape.
  - `*CAT` never ends by itself; it runs until Escape (DIS 17734).
  - SAVE: the motor runs from the RECORD prompt to the end of the final tail.

### 2.2 How the MOS uses DCD and the receive interrupt

IRQ dispatch goes from `.irq1Handler` to `.irq1CheckACIA` to `.updateACIA` when status bit 7 is set and the tape owns the ACIA (DIS 10679, 10705, 18353). `.updateACIA` reads status, then reads the data register, shifts status bit 2 (DCD) into carry, and runs a state machine held in &C2 (DIS 18421-18527):

1. **Waiting for carrier.** Wait for an interrupt with DCD set. During leader the only possible interrupt is the DCD edge, because all-ones data contains no start bit.
2. **Waiting for sync.** Ignore interrupts while DCD still reads set. On the first received byte with DCD clear: if it is **&2A**, clear the CRC and go to 3. Otherwise blip the motor, reprogram the ACIA and go back to 1, so a false start costs a fresh DCD.
3. **Header.** Each byte goes to the foreground. A DCD during the header cuts it short (SRC MOS74 419, 429; DIS 18748-18781).
4. **Data.** Bytes are CRC'd and stored, or sent through Tube R3 for a coprocessor address. **A DCD during data resets the ACIA and abandons the block**; the foreground then reports `Data?`.
5. **Data CRC.** The two CRC bytes, then an ACIA reset.

- **No error bits are checked.** The ACIA's framing, parity and overrun bits are never examined on the tape path.
- **What the MOS requires of the hardware:**
  - a DCD edge interrupt during leader;
  - DCD reading clear again by the time the &2A arrives;
  - bytes delivered with receive interrupts.

  This is consistent with the measured pulse (section 1.6), and not with the AUG's "DCD high = no carrier" wording.

### 2.3 Block format and CRC

Sources: DIS 17089-17128; UG ch.35 §S; AUG §16.10, pp.346-347.

| Field | Bytes | Notes |
|---|---|---|
| Leader | — | 2400 Hz carrier; see 2.4 |
| Sync | 1 | &2A, not covered by the CRC |
| Filename | 1-10 + &00 | The reader keeps at most 10 characters |
| Load address | 4 | Little-endian; top 16 bits &FFFF means the I/O processor |
| Exec address | 4 | Little-endian |
| Block number | 2 | Little-endian, from 0 |
| Block length | 2 | Little-endian; 0-256 in practice |
| Flag | 1 | Bit 7 = last block; bit 6 = empty block (never set by MOS 1.20, so test the length); bit 0 = locked (*RUN only; `*LOAD` gives `Locked`) |
| Spare | 4 | &00 on tape |
| Header CRC | 2 | High byte first, covering filename to spare |
| Data | length | Absent when the length is 0 |
| Data CRC | 2 | High byte first, covering the data. **Absent when the length is 0**: SAVE skips both data and data CRC (DIS 19006-19008), and the reader does not enter the data state (DIS 18938-18942). Not every third-party decoder agrees (section 7.3) |

- **CRC.** CRC-16/XMODEM: polynomial &1021, initial value 0, unreflected (DIS 18870-18900; AUG §16.10 lists the 6502 routine). The check value for "123456789" is &31C3 (`~/Code/oaknut/packages/oaknut-romfs/src/oaknut/romfs/crc.py`).
- **Trailing bytes.** After the data CRC the MOS waits for two more transmit-empty interrupts before resetting, and each one re-sends the last byte (DIS 19027-19029).
  - A MOS SAVE captured by BeebEm (`~/Code/beebem-mac/UserData/Tapes/Test.uef`) shows each block ending in the CRC high byte followed by the low byte three times.
  - Readers must ignore whatever follows the data CRC.

### 2.4 Leader and gap timing on SAVE

`.generateDelay` counts VSyncs. Each delay of A lasts (A+1)/10 s, not A/10 s (DIS 19107-19119). Timeline of a SAVE:

1. The dummy &AA is sent at the prompt and again after RETURN.
2. **5.1 s** of carrier (A = 50).
3. Block 0.
4. A 0.2 s tail after each block.
5. A pre-block gap of 0.7 s for LOAD/SAVE, or 2.6 s by default for sequential files (`*OPT 3,n`).

So consecutive SAVE blocks are separated by about **0.9 s** of carrier (DIS 18964-18967, 19032, 19709, 17166, 18286-18295). After the last block there is a 5.1 s tail, a bell, and the motor goes off.

- **Check against a real recording.** The BeebEm-recorded `Test.uef` contains the pair of &AA bytes 0.29 s apart, 5.06 s of tone before block 0, 0.88 s between blocks, and 5.26 s after the last block. That matches the code.
- **Mastered commercial tapes do not follow these timings.** The Acorn Welcome tape has 0.62 s of leader, a dummy &DC, then 0.62 s, and 0.25 s between blocks (parse of `~/Code/b-em/tapes/Welcome_B.uef`). Short leaders must be accepted, and that bears directly on the DCD threshold.

### 2.5 Errors, timeouts and retry

- **No timeouts.** MOS 1.20 never times out a tape read. Every wait loops on the state flag and checks only for Escape (DIS 18742-18746). So "Data?" with no tape playing is an endless `Searching`.
- **Errors raised** (`.loadBlock`, DIS 19376-19447):

  | Error | Code | Raised when |
  |---|---|---|
  | `Data?` | &D8 | Header CRC or data CRC fails |
  | `Block?` | &DA | Block number is not the next one expected |
  | `File?` | &DB | A different filename's header arrives mid-load |
  | `Header?` | &D9 | Defined in the source (SRC MOS70 59-68) but **never raised by 1.20**; a header CRC failure reports `Data?` instead (SRC MOS76 100). UG ch.46 lists it, so treat it as an earlier-MOS message |
  | "Bad tape" | aliased to &D8 | Defined in the source, never raised |
- **What happens after an error** (DIS 19449-19626, 17519). The `*OPT 2` setting decides:
  - **Ignore:** print the error and carry on; CRCs are not checked at all.
  - **Retry:** the LOAD/SAVE default. Print the error, then BEL and `Rewind tape`, then `Searching`. The motor stays on until the failed block comes round again.
  - **Abort:** BRK with the error and the motor off.
- **Sequential access** (BGET, BPUT, *EXEC, *SPOOL) aborts with a trappable error by default (UG §Q).
- **`*OPT 1,n` messages.** 0 gives none, 1 short, 2 long; the default is short. The short form is a CR, the filename padded to 11, and the block number in hex. The long form adds the load and exec addresses (DIS 19145-19250; AUG §2.14; UG §R).

---

## 3. Prior art (read, not copied)

### 3.1 b2

- **No working tape.**
  - Master plumbs a SERPROC to a bit-level 6850 through a byte-oriented `SerialDataSource` that nothing sets. In cassette mode /DCD is held high (serproc.cpp `Write`).
  - The upstream `wip/tom` branch adds a chunk-naming UEF debug window, gzip support and load/eject messages behind `ENABLE_TAPE 0`, whose comment calls it "super WIP, and non-functional" (`conf.h:184-185` on that branch).
- **Clocking.**
  - The SERPROC is ticked at 4/13 MHz as a stand-in for 16/13 MHz, so 19200 and 9600 cannot be divided correctly (a TODO in serproc.cpp).
  - The 6850 applies /1, /16 and /64 (`MC6850.cpp:101-117`).
  - Tape mode still uses the receive field, contrary to the BeebWiki note the author recorded in `doc/b2_notes.org`.
- **Everything else is absent.** No CSW, no recording, no catalogue, no tape position in savestates. The motor shows only as an LED (`BeebWindow.cpp:1771`).
- **Useful for us:** the clocking model and the research notes in `b2_notes.org`, around lines 5000-5054.

### 3.2 BeebEm

- **Byte injection.** Whole bytes are written into the ACIA receive register (`Serial.cpp:424-458`). A tape tick runs every 2 MHz / 5600 cycles; the provenance of 5600 is unknown even to the maintainers ("not sure why its 5600", `Uef.cpp:41`).
- **The tape waits for the CPU.** It advances only while the ACIA has room (`Serial.cpp:685-696`, 751-762), so it never overruns as a real tape would.
- **ULA field and ACIA divide** are ignored on receive.
- **DCD.** A 200 µs pulse raised after 200 ms of tone, or 5 ms at fast speeds (`Serial.cpp:516-555`, 765-789). The comment cites a stardot post, p=327136.
  - The DCD status bit is cleared on a data read.
  - Entering RS423 forces DCD low, citing AUG p.389.
- **300 baud.** UEF &0113 rescales the timeline. &0104 is assumed to be 8N1.
- **UEF** (`Uef.cpp:288-576`):
  - Plain and gzipped files are both read (`gzopen`). A chunk at &0200 or above fails the load.
  - Handled: &0100, &0104, &0110, &0111, &0112, &0113, &0116 (with its own float decoder), and &0114 approximately, as data bytes.
  - Metadata chunks are kept but ignored.
  - &0101, &0102, &0115, &0117 and &0120 are ignored, so &0102 data is lost.
- **CSW** (`Csw.cpp`):
  - Only v2 Z-RLE really works; compression and flags are read but unused, so polarity is ignored. Output goes into a fixed 8 MiB buffer.
  - Pulses are classified with fixed thresholds of 13 and 20 samples, correct only at 44.1 kHz. Tone needs more than 20 short pulses (an "arbitrary figure").
  - 300 baud is decoded by averaging and, by the code's own comment, runs four times too fast. The tape-map builder sets the ACIA divider from the UI side to detect it.
- **Recording.**
  - UEF only (&0000, &0100, and &0110 at 1200-baud timing), held in memory, appended at the end, and written gzipped on Stop (`Uef.cpp:72-233`; `TapeControlDialog.cpp:248-293`).
  - Bytes sent while not recording are swallowed after one byte time so loaders do not hang (`Serial.cpp:611-626`).
- **Tape map.** A list of "NAME blk Length" rows built by replaying the tape, with "Non-standard" for other blocks (`Uef.cpp:601-734`). The highlighted row is the position, and clicking a row seeks there.
  - Controls: Play, Stop, Rewind, Eject, Load, New, Record, and Unlock, which clears a block's locked bit and patches the header CRC (`Uef.cpp:738-811`). There is no fast-forward.
- **Motor.** Motor off stops the tape. There is an LED, optional relay-click samples and synthesised tape audio (`Serial.cpp:359-371`; `Sound.cpp:390-427`, 792-805).
  - A loaded tape plays as soon as the motor turns on. With no tape the MOS waits forever.
- **Speed and savestates.**
  - Fast-tape settings compress tape time by up to about 7.5×, which works only because the tape waits for the CPU.
  - Savestates use chunk &0473 (ACIA, ULA, tape clock, carrier timer, CSW decoder). The image is reopened by path, and saving state is disabled while recording.

### 3.3 B-Em

- **Byte injection.** One whole byte per character time. A poll every 64 µs counts down 130 polls at 1200 baud (`uef.c:34`; `6502.c:1078-1088`).
  - There is no shift register, framing or overrun; an unread byte is overwritten (`acia.c:116-122`).
  - The ULA field is ignored (`serial.c:25-55`). The ACIA divide is used only by the CSW decoder.
- **DCD is a level with no delay.**
  - Raised on entering a &0110 or &0111 chunk and dropped at a data chunk (`uef.c:114-198`). Gaps leave it alone.
  - With CSW it is raised at load and toggled by the decoder (`csw.c:78`, 137-190).
- **300 baud.** UEF &0113 rescales the poll. CSW at /64 skips pulses. &0117 is ignored.
- **UEF** (`uef.c:29`, 102-262):
  - Gzipped files work, but the magic is not checked.
  - Handled: &0100, &0104 (data-bit count only), &0110, &0111, &0112 (/20 polls), &0113 and &0116.
  - Skipped: &0114, &0115, &0117 and all metadata.
  - **At end of tape it loops back to the start.**
- **CSW** (`csw.c:31-190`):
  - It assumes the v2 header without checking, so v1 is misparsed. It inflates Z-RLE into 8 MB, silently truncating, and ignores the sample rate.
  - Pulses are classified against a single 13-sample threshold. The &00 long-pulse escape is not handled, polarity is ignored, and carrier plays about 4× slow.
- **Recording.** None. Transmitted bytes can go to a raw byte file (`sysacia.c:21-61`).
- **Catalogue.** B-Em is the only one of the four with a catalogue. It rewinds, scans offline for &2A after carrier, parses the headers, and prints name, size, load and exec to a log window (`uef.c:286-397`; `csw.c:210-316`). There is no position counter.
- **Motor.** Motor off pauses the tape. The motor LED goes out two emulation slices after the motor stops (`main.c:650-655`).
- **No tape** means the MOS waits.
- **Fast tape** frame-skips and free-runs while the motor is on; tape timing itself is unchanged.
- **Savestates** hold only the ACIA and ULA registers. Tape identity and position are not saved.

### 3.4 beebjit (the most careful model)

- **A pre-rendered bit stream.** UEF and CSW are converted at load time into symbols 0, 1 and silence (`tape.c:128-175`). A timer hands one symbol every 1666 cycles (1200 bit/s) to the ULA, which forwards it to a bit-level 6850 (`tape.c:15-72`; `serial_ula.c:215-256`; `mc6850.c:265-333`).
- **The ACIA is genuinely bit-level.** It detects framing and parity errors, and raises overrun only after the earlier byte has been read (`mc6850.c:244-263`, 385-392). Silence sends no bits.
- **ULA field.** Ignored for tape (`serial_ula.c:168-185`).
- **/64.** The ACIA keeps 1 bit in 4 using a free-running counter (`mc6850.c:266-277`).
- **300 baud.** UEF &0117 repeats every bit four times (`tape_uef.c:372-400`). &0113 is ignored. The author's own comment calls this "not a good emulation".
- **DCD:**
  - The carrier counter counts consecutive 1 bits and resets on a 0 or on silence. At exactly 240 bits (0.2 s) DCD is raised for one bit time (`serial_ula.c:215-245`).
  - The provenance is documented: the service-manual 0.1-0.4 s range, Ferranti measurements, and the real blip of about 210 µs. Commit 8b6637b set the value to 0.2 s.
  - The latch clears on a data-register read (`mc6850.c:114-178`).
  - Motor off sends silence, which resets the counter (`tape.c:194-196`).
- **UEF** (`tape_uef.c:57-407`):
  - Gzip is detected by its magic. The "UEF File!" signature and major version 0 are checked.
  - &0104 gets real parity and stop bits.
  - It sniffs the &0000 origin string and, for MakeUEF before version 2.4, swaps odd and even parity to work round a MakeUEF bug.
  - &0116 is accepted up to 1200 s.
  - &0113, &0114 and &0115 are ignored. Unknown chunks are logged and skipped.
- **CSW** (`tape_csw.c`):
  - v2 only, checked; RLE or Z-RLE; the &00 escape is handled; the sample rate is honoured.
  - It classifies whole cycles: two half-waves for a 0, four for a 1, within ±25% of nominal, tightened to ±18% in carrier.
  - A silence/carrier/data state machine provides phase lock, and polarity is irrelevant.
- **Recording.** None. A cassette-mode transmit byte is consumed at once (`serial_ula.c:35-47`); Pro Boxing Simulator needs that.
- **ACIA master reset clears the control register**, against the datasheet. Frak needs this (`mc6850.c:404-426`).
- **No UI.** Up to four tapes can be loaded, and Alt-T cycles between them. `-fasttape` unthrottles the emulator while the motor runs, so emulated timing stays exact. There is no tape position in savestates.
- **Test corpus.** `test/tapes/README` documents 18 tapes, each with a SHA-256 and the behaviour it stresses. No automated test uses them. Highlights:

  | Tape | Stresses |
  |---|---|
  | CavemanCapers | DCD latch and clear |
  | StarDrifter | DCD timing with very short carrier |
  | AticAtac | motor toggled during tone |
  | MissileControl | bytes accepted outside carrier sync |
  | ProBoxingSimulator | transmit accepted during a load |
  | Frak | master reset clears CR |
  | Fortress (CSW) | 8N2 with a bad second stop bit |
  | Doctor Who and the Mines of Terror (300-baud protection), Swarm (CSW) | 300 baud |
  | Uridium (CSW) | tone, data, tone, data with no gap: DCD must rise again |
  | Jet Set Willy, Starquake, Exploding Fist (CSW) | phase and threshold sensitivity |

### 3.5 jsbeeb and MAME (brief)

- **jsbeeb.**
  - Bytes are delivered whole at the stop bit; per-bit timing drives only the tape sound (`tapes.js:70-131`).
  - DCD is a one-bit pulse at exactly 209 carrier bits, about 0.174 s, "measured on an issue 3 model B". It is latched until a data read (`acia.js:69-167`).
  - &0113 changes the base frequency. &0117 and CSW are absent, and there is no recording.
  - Two apparent bugs: &0110 tones play twice as long as intended, and the &0112 formula looks inverted.
  - jsbeeb issues #104, #241, #257 and #262 are the origin of several entries in beebjit's corpus.
- **MAME.** The only waveform-level model, and the only one that records.
  - It times zero-crossings and sends 16 RxC pulses per recovered bit, so the ACIA divider does the work.
  - DCD is held as a level after more than 100 ones (about 83 ms), which contradicts the measurements.
  - Recording synthesises half-cycles of 18 and 9 samples at 44.1 kHz from TxD.
  - Section 7.7 covers its UEF-to-waveform path in detail.
- **B-Em-TOH** (not local). A "tape overhaul" fork of B-Em, version 4.4, from https://www.beebstuff.uk/f/acorn/TOH/v4.4/ (readme there). It grew out of T30963 and is the emulator its authors used to measure title sensitivity.
  - It models motor off resetting DCD, and a "strip leader" turbo mode using a DCD delay of about 0.04 s (T30963 P1/p454535, P6/p456492).
  - It ships the tape test suite described in section 5.5.
  - Not read for this survey. It is a candidate for round 2.

### 3.6 What nobody does

No reference does any of the following:

- implement &0114 security cycles;
- keep the tape position in a savestate;
- record to UEF bit-accurately: BeebEm records bytes at 1200-baud timing, and MAME records a waveform;
- model the analogue envelope behind DCD;
- run a tape corpus as automated tests.

These are open ground for the specification.

---

## 4. The formats

### 4.1 UEF

- **Copies of the specification.** No copy exists locally. Format version 0.10 is the current version, and two drafts of it are public:
  - **Draft 28, revised 10/1/2006: the canonical copy.** Archived from the original host at https://web.archive.org/web/20260808124008/http://electrem.emuunlim.com/UEFSpecs.htm (the live host no longer responds). The same text is published in the PlayUEF project's `docs/UEFspecification.html`.
  - Draft 25 (2005): https://mdfs.net/Docs/Comp/BBC/FileFormat/UEFSpecs.htm. Where it differs from draft 28, the difference is noted below.
- **Wikipedia.** https://en.wikipedia.org/wiki/Unified_Emulator_Format cites draft 28 as the latest release. It notes that the header version matters because "one chunk has been redefined between versions"; that chunk is &0113. It lists ElectrEm, BeebEm and B-Em, and several standalone tools, as UEF consumers.
- **Layout.**
  - A header of `UEF File!` and NUL (10 bytes), then the minor version byte, then the major version byte.
  - Then chunks, each a u16 id and a u32 length (excluding the 6-byte chunk header) followed by the body.
  - Everything is little-endian. Floats are 4-byte IEEE 754 single precision, little-endian.
- **Gzip convention.** Files are commonly gzip-compressed while keeping the `.uef` name. Detect the `1F 8B` magic and inflate (draft 28 "Conventions"; beebjit `tape_uef.c:57-60`). MakeUEF writes v0.5 headers; BeebEm writes v0.10.
- **Wave model** (draft 28):
  - A cycle is a low pulse followed by a high pulse.
  - The base frequency defaults to 1200 Hz, and carrier is at twice the base.
  - "1200 baud" encoding: 0 = 1 cycle at base, 1 = 2 cycles at 2×base. "300 baud" encoding: 0 = 4 cycles, 1 = 8 cycles.
  - The default phase is 180°.
  - The specification says &0100, &0110 and &0111 alone give about 80% compatibility.

**Chunk list** (draft 28; draft-25 differences noted). "BBC" marks what matters for Model B family tapes: **Y** must handle, **y** should handle, – metadata or irrelevant.

| Id | Name | BBC | One line |
|---|---|---|---|
| &0000 | origin information | – (display) | Creating tool's text, e.g. "MakeUEF 0.3b"; beebjit uses it to detect the MakeUEF parity bug |
| &0001 | instructions / manual or URL | – (display) | Text, or "URL: …" |
| &0003 | inlay scan | – (display) | Width, height, bpp, optional palette, pixels |
| &0005 | target machine | – (display/check) | High nybble: 0 Model A, 1 Electron, 2 Model B, 3 Master, 4 Atom; low nybble: keyboard preference |
| &0006 | bit multiplexing information | – | Multiplier byte |
| &0007 | extra palette | – | RGB triples |
| &0008 | ROM hint | – | Requests a ROM or class present or absent |
| &0009 | short title | – (display) | ASCII title |
| &000A | visible area | – | Screen rectangle |
| &0100 | implicit start/stop bit data | **Y** | Bytes sent as 8N1, LSB first; the MOS blocks live here |
| &0101 | multiplexed data | – | Multiplexed form of the preceding data chunk (&0103 is a deprecated synonym) |
| &0102 | explicit tape data | y | Raw bit stream; the first byte gives the unused trailing bits; LSB first; poorly supported by emulators |
| &0104 | defined tape format data | **Y** | Three bytes (data bits per packet; parity N/E/O; stop bits, where draft 28 makes a negative count mean \|n\| stop bits plus one extra short wave), then data; start bit implicit. BBC formats seen: 7E1, 7E2, 7O1, 7O2, 8E1, 8N2, 8O1 |
| &0105 | Atom implicit data (draft 28) | – | As &0100 with an extra wave after each stop bit |
| &0110 | carrier tone | **Y** | u16 count of cycles at 2×base (draft 25 expressed it in 1/(2×baud) units, numerically the same at 1200) |
| &0111 | carrier tone with dummy byte | **Y** | u16 cycles, the bits of &AA as 8N1, u16 cycles; mirrors the AUG §20.10 dummy byte |
| &0112 | integer gap | **Y** | u16 n: silence of n/(2×base) s. Draft 28's text has a typo, 1/(2n·base); draft 25, beebjit and MAME use n/(2×base) |
| &0113 | change of base frequency | y | Float, the new base frequency in Hz. Draft 25 called it "change of baud rate" |
| &0114 | security cycles | y | 24-bit cycle count; byte 3 'P' or 'W' (first cycle is a lone high pulse); byte 4 'P' or 'W' (last cycle is a lone low pulse); then a bit field of cycles, 1 = short (2×base), 0 = long (base). **Bit order is ambiguous** (below) |
| &0115 | phase change | – | u16, 0-359 degrees; meaningful only next to a gap |
| &0116 | floating-point gap | **Y** | Float, seconds of silence; beebjit has seen 907.9 s |
| &0117 | data encoding format change (draft 28) | y | u16 300 or 1200: selects the 4/8-cycle or 1/2-cycle bit encoding; protected tapes switch speed with it |
| &0120 | position marker | – (display) | Text naming a place on the tape |
| &0130 | tape set info (draft 28) | – (display) | Vocabulary, number of tapes, number of channels |
| &0131 | start of tape side (draft 28) | – (display) | Tape id, side bit, channel, description |
| &0200-&0210 | disc chunks | – | Disc images |
| &0300, &0301 | ROM | – | ROM images |
| &0400-&0420 | machine state (6502, Electron ULA, WD1770, JIM, memory, Slogger board) | – | Savestate chunks; a tape player should ignore or reject them |
| &FF00-&FFFF | emulator identification and private chunks | – | |

- **&0114 bit order: the canonical draft contradicts itself.**
  - **The prose:** slow cycles are 0, fast cycles are 1, and the most significant bit is the first cycle on the tape.
  - **The worked examples:** "LSLLLSSLSSLLSL" is stored as &9D &2C, and "one short pulse then three long waves" as &0E. I checked all three bytes by hand. They decode only if **long = 1 and the least significant bit is the first cycle**, which is draft 25's wording ("long waves are 1 bits … stored akin to data bytes").
  - **Other implementations:** B-Em, beebjit and MAME all skip the chunk. Quadbike's documentation says &0114 is the only general cycle-level chunk and that Quadbike uses it for arbitrary data (`docs/html/index.html`, "UEF Chunk &114").
  - **What to do:** treat the bit order as unresolved until it can be tested against real files.

### 4.2 CSW

- **Authorship.** The CSW specification is Ramsoft's, revision 2.00, August 2003. It is not by the author the brief names.
  - Original location: http://www.ramsoft.bbk.org/csw.html
  - Mirror: https://k1.spdns.de/Develop/Projects/zxsp/Info/File%20Formats/CSW%20technical%20specifications.html
- **v2 header** (little-endian):

  | Offset | Field |
  |---|---|
  | &00 | "Compressed Square Wave" (22 bytes) |
  | &16 | &1A |
  | &17 | Major version, 2 |
  | &18 | Minor version, 0 |
  | &19 | u32 sample rate |
  | &1D | u32 pulse count after decompression |
  | &21 | Compression: 1 = RLE, 2 = Z-RLE |
  | &22 | Flags: bit 0 = initial polarity (set = signal starts high) |
  | &23 | Header-extension length HDR (reserved for future revisions) |
  | &24 | 16-byte encoding-application name |
  | &34 | HDR bytes of extension |
  | &34+HDR | Data |

- **v1.01 header.** The same signature, &1A and version bytes (major 1, minor 1). Then:
  - a **u16** sample rate at &19;
  - compression at &1B (RLE only);
  - flags at &1C (the polarity bit was added in 1.01);
  - 3 reserved bytes;
  - data from &20.
- **RLE.** One byte per pulse (half-cycle) gives its length in samples. A pulse longer than 255 samples is written as &00 followed by a u32 length.
- **Z-RLE.** The same stream deflated with zlib (B-Em `csw.c:56-66`; MAME `csw_cas.cpp`).
- **What the format does not record.** It carries no phase. Silence can only be a long pulse.
- **Compatibility.** B-Em and BeebEm assume 44.1 kHz and a fixed v2 header. beebjit honours the rate but rejects v1.
  - For files that every emulator reads: v2, Z-RLE, HDR = 0, 44100 Hz.
  - For exactness: at 48 kHz the half-cycles are exactly 10 samples (2400 Hz) and 20 samples (1200 Hz).

---

## 5. Test material

### 5.1 Tape images already on this machine

| Image | Notes |
|---|---|
| `~/Code/b-em/tapes/Welcome_B.uef` (byte-identical to `~/Code/jsbeeb/public/tapes/Welcome.uef`) | The Acorn Welcome tape, MakeUEF 0.3b, UEF v0.5, chunks &0110/&0100/&0112. A real mastered tape with short leaders (0.25 s between blocks) and dummy byte &DC. Acorn/BBC copyright, widely redistributed with emulators but not licensed. |
| `~/Code/b-em/tapes/Welcome-BBCMaster-Side1_BM.uef`, `…Side2_BM.uef` | Master Welcome tape; same caution. |
| `~/Code/beebem-mac/UserData/Tapes/Test.uef` (also in `~/Code/beebem-windows`) | Gzipped, v0.10, "Created by BeebEm". One BASIC program, 6 blocks. A MOS 1.20 SAVE captured by an emulator, and the best local reference for MOS write timing (section 2.4). Ships with GPL BeebEm; authorship of the program unknown. |
| `~/Code/acornaeology/dunjunz/versions/dunjunz-tape/Dunjunz_B.uef` | Commercial (1987), multi-file with a custom loader. Do not commit. |
| `~/Code/beebjit/test/tapes/` (11 UEF, 7 CSW) | Commercial titles; the README gives SHA-256s and what each stresses (section 3.4). A ready-made acceptance list. Use by reference or fetch at test time; do not commit. |
| `~/Code/oaknut/images/WAV_tapes/*.wav` + `.toml` | A corpus of real recordings supplied for the project; inventoried in section 5.6. |
| `tests/assets/` | No tape images. |

### 5.2 Tools that can build a tape from a known program

- **No local tool writes a UEF from a program in CI.**
  - beebasm emits SSD/DSD or raw binaries only.
  - beebtools and basictool are disc and BASIC only.
  - oaknut-file, oaknut-dfs and oaknut-zip have no tape support.
- **oaknut-romfs already has the cassette block codec**, because ROMFS uses the tape block format: &2A, header, flags, CRC-16/XMODEM high byte first (`~/Code/oaknut/packages/oaknut-romfs/src/oaknut/romfs/block.py`, `crc.py`).
- **oaknut-basic** tokenises BBC BASIC, so it can supply the program bytes.
- **Quadbike 2.0.5:** decodes WAV into CSW or TIBET, and its documentation describes companion scripts that include TIBET to UEF.
  - Its licence is unclear, so only its HTML documentation has been read (section 7.2).
- **DiscImageManager** (GPL-3): writes UEF, but is a GUI application.
- **acornaeology's `read_uef.py`** (`~/Code/acornaeology/dunjunz/tools/read_uef.py`): a small UEF reader that catalogues and checks both CRCs. Useful as an independent oracle.
- **PyPI.** Nothing is published under the obvious names (uef, pyuef, bbc-uef, uef2wave, playuef, makeuef, uefwalk). The GitHub scripts found (bin2uef/uef2wave, an acorn-tape-reader, the UEFfile.py of the UEFtrans tools, PlayUEF) are GPL or declare no licence. Vendoring them is unattractive.
- **Generating a tape in the test is simple** **[inferred]**, following section 2.4:
  1. an &0000 origin chunk;
  2. &0110 leader of 12240 cycles (5.1 s), or the &0111 form with &AA;
  3. for each block, one &0100 chunk holding &2A, the header, its CRC, the data and its CRC, followed by &0110 of about 2160 cycles (0.9 s);
  4. a tail.

  That is a few dozen lines with the CRC included. Bytes can come from `oaknut-basic tokenise` or beebasm.

### 5.3 CSW from UEF, and back

- **UEF to CSW is mechanical**, because every UEF tape chunk is defined in whole cycles.
  - At 48 kHz, half-cycles are exactly 10 and 20 samples. At 44.1 kHz they are 9.1875 and 18.375, and need an error-accumulating rounder.
  - Gaps become long pulses using the &00 escape.
  - A 300-baud bit is 4 or 8 cycles.
  - Emitting whole cycles avoids the polarity inversion that an odd number of half-cycles causes.
- **CSW to UEF** has no convenient open route: MakeUEF is closed and Windows-only, and Quadbike's route goes through WAV and TIBET. For a round-trip test the emulator's own decoder is the natural reader: load the UEF and the generated CSW, and assert the same machine-visible outcome (memory after `*LOAD`, or `*CAT` output).

### 5.4 The existing pattern to follow

- **C++.** Tests find beebasm on PATH or through `$BEEBIUM_BEEBASM`, assemble an SSD at test time from a checked-in `.asm`, and `SKIP` if it is absent. Examples: `tests/test_serial_break_e2e.cpp:94-120` with `tests/assets/serial/serial_break.asm`, and `tests/test_voltmace_delta_14b.cpp:129ff`.
- **Python.** Tests run `uvx oaknut-basic tokenise` and `oaknut-disc create`/`put`, then CHAIN the result (`clients/beebium-python-client/tests/test_controller.py:16-60`). `oaknut-dfs` serves as an independent reader for round trips (`tests/test_storage_round_trip.py`).
- **Synthetic assets.** These are documented with regeneration steps in `tests/assets/discs/README.md`.
- **A tape analogue** **[inferred]**: tokenise or assemble at test time and wrap the bytes with an in-repo UEF/CSW writer. A CFS block writer may belong in oaknut eventually.

### 5.5 Tape-timing test suites (T30963 and p441128)

- **The ACIA timing test.** https://stardot.org.uk/forums/viewtopic.php?p=441128#p441128 (thread t=25152) is a test of how long the ACIA takes from a transmit write to TDRE, across all 8 ULA rates and 3 ACIA dividers.
  - Results on a Ferranti Model B and on a VLSI Master were posted as photographs.
  - The delay is spread evenly over 0.5 to 1.5 bit times, 420-1260 µs at 1200 baud.
- **The B-Em-TOH test suite.** It ships as `tapetests-7.zip`, 327 files, at https://www.beebstuff.uk/f/acorn/TOH/v4.4/ (T30963 P2/p454736). Contents:
  - DCD tests written for the replacement-ULA work, including "motor off resets the DCD timer";
  - save tests at 75-600 baud;
  - TDRE histogram and range tests;
  - UEF, CSW and TIBET format cases;
  - about 30 protected commercial games, which overlap beebjit's corpus: Arcadians, Atic Atac, Caveman Capers, Fortress, Ultron, Video's Revenge, Pro Boxing Simulator and more.
- **Licence.**
  - Its PHP runner is GPL-2.0-or-later and drives B-Em only, so it is not reusable as it stands.
  - The BBC-side test programs and tape images could serve as test vectors under a Beebium harness.
  - No licence is stated for the fixtures, and the games are commercial. Committing any of it needs a decision; fetching at test time is the alternative.
- **Audio test material.**
  - The same contributors' audio captures of the protected games.
  - Two DCD-test WAV sets, at https://www.beebstuff.uk/f/acorn/serial-ULA-DCD-test/ and `.../serial-ULA-DCD-test-2/` (T30963 P5/p456422, P6/p456685).

  These are analogue-input cases for a waveform path.
- **Results need tolerances.** Results vary between individual Ferranti chips, and TDRE timings are spreads, so tests built on them need tolerances.

### 5.6 A corpus of real recordings

A corpus of recordings supplied for the project is held at `~/Code/oaknut/images/WAV_tapes`: 19 WAV files, 3.0 GB in total.

- **Handling.** They are third-party recordings of commercial tapes. They must never be copied into this repository or modified, so tests can only reference them in place, read-only.
- **Metadata.** Each WAV has a TOML file beside it with these fields: tape, side, take, contents, baud, signal channel, recording application, source, machine, deck, sound card, date, recording level, tape condition, known problems and notes.
- **Common to every recording:**
  - 1200 baud;
  - signal on the left channel;
  - played on an Acorn data recorder and captured through a USB sound card's line-in;
  - tape condition recorded as "good".

Format and duration were measured read-only with Python's `wave` module.

| Recording | Rate | Channels | Bits | Duration | TOML level | TOML known problems |
|---|---|---|---|---|---|---|
| cascade50-side1-001 | 48000 | 2 | 16 | 1705.9 s | normal | Several bad patches that need rewind and replay on a BBC Micro |
| cascade50-side1-002 | 48000 | 2 | 16 | 1708.1 s | normal | same |
| cascade50-side1-003 | 48000 | 2 | 16 | 1709.9 s | normal | same |
| cascade50-side1-004 | 48000 | 2 | 16 | 1704.8 s | high | same |
| cascade50-side2-001 | 48000 | 2 | 16 | 1704.8 s | normal | same |
| cascade50-side2-002 | 48000 | 2 | 16 | 1701.5 s | normal | same |
| cascade50-side2-003 | 48000 | 2 | 16 | 1696.9 s | normal | same |
| palace-of-magic-side1-001 | 48000 | 2 | 16 | 588.3 s | normal | unknown (side 1 is the BBC Micro version) |
| palace-of-magic-side1-002 | 48000 | 2 | 16 | 587.3 s | normal | unknown |
| palace-of-magic-side1-partial-rewind | 48000 | 2 | 16 | 128.6 s | normal | A sample, not a full tape: play from the start, stop part way, rewind a few seconds, play again |
| palace-of-magic-side2-001 | 48000 | 2 | 16 | 586.2 s | normal | unknown (side 2 is the Electron version) |
| palace-of-magic-side2-002 | 48000 | 2 | 16 | 587.7 s | normal | unknown |
| plan-b2-side1-001 | 48000 | 2 | 16 | 296.4 s | normal | unknown (both sides carry the same program, for BBC Micro and Electron) |
| plan-b2-side1-002 | 48000 | 2 | 16 | 298.3 s | normal | unknown |
| plan-b2-side1-level-high | 48000 | 2 | 16 | 306.8 s | high (deck volume 3.5 of 10) | unknown |
| plan-b2-side1-level-low | 48000 | 2 | 16 | 299.0 s | low (deck volume 0.5 of 10) | unknown |
| plan-b2-side1-level-normal | 48000 | 2 | 16 | 302.5 s | normal | unknown |
| plan-b2-side2-001 | 48000 | 2 | 16 | 296.7 s | normal | unknown |
| plan-b2-side2-002 | 48000 | 2 | 16 | 296.7 s | normal | unknown |

**Signal measurements.** These are my own measurements, read-only, over 20-50 s into each file and a 60 s window for the histograms. They are a first look, not a characterisation.

- **Level.**
  - Normal-level takes peak at 52-95% of full scale with an RMS of 21-36%, and the DC offset is negligible.
  - **The two "high" takes clip hard.**
    - cascade50-side1-004: peak 100%, RMS 74%, 22% of samples clipped, DC offset about −2.7% of full scale.
    - plan-b2-side1-level-high: RMS 81%, 42% of samples clipped, a DC offset, and about 6% crosstalk into the right channel.
  - **The low take peaks at 18%** with an RMS of 5%.
  - The right channel carries 0.3-0.9% RMS crosstalk on normal takes.
- **Half-cycle lengths** (zero-crossing intervals about the mean, in samples at 48 kHz; nominal 10 for 2400 Hz and 20 for 1200 Hz).
  - **Normal take (plan-b2-side1-level-normal):** sharp peaks at 10 (with 9 and 11) and at 20 (with 19 and 21). 78% of intervals fall in 7-13 and 19% in 16-24.
  - **Clipped high take:** the peaks at 10 and 20 are, if anything, sharper. Clipping turns the signal into a square wave, which is what the ULA's own squaring stage does anyway.
  - **Low take:** with this naive detector (a crossing of the mean, no hysteresis, no filtering), the distribution is smeared almost flat from 6 to 14 samples, and only 50% fall in 7-13. Noise near the zero line causes extra crossings.
  - So low recording level, not clipping, is the hard case for a zero-crossing decoder. Filtering and hysteresis, or the analogue front end's band-pass (section 1.5), are needed for a decoder to cope with it.
- **Coverage.** Together the corpus gives the specification:
  - three recording levels of one program;
  - repeated takes of the same side;
  - known bad patches with a known remedy, rewind and replay;
  - a deliberate stop-rewind-replay;
  - an Electron side, useful as a negative or compatibility case.

---

## 6. Cassette presence per variant

| Machine | Cassette | Evidence |
|---|---|---|
| Model A | Yes (not a Beebium variant) | SM-B §1.2.1 (PDF p.11): cassette at 300/1200 baud, CUTS, motor control, 7-pin DIN; AUG §16.10 (p.346): the CFS is standard on all BBC microcomputers |
| Model B | Yes | SM-B §1.2.2, §3.5 (PDF p.29: IC4 6850, IC7 2C199, IC35, Q3); UG ch.1 "Leads" (pp.12-13) |
| Model B+ 64K | Yes | SM-B+ §1 (PDF p.9), §3.5 (PDF pp.11-12), §5.12 (PDF pp.30-31), SK5 (PDF p.64) |
| Model B+ 128K | Yes **[inferred]** | Same motherboard; SM-B+ RAM control covers both sizes (PDF p.24); AN 029/030 describe only the extra sideways RAM |
| Model B with ROM/RAM boards (`model-b-romram`, `-atpl-sidewise`, `-watford-rom-ram`, `-integra-b`) | Yes **[inferred]** | Model B motherboard with add-on boards that use none of &FE08-&FE1F; the cassette circuitry is untouched |
| Master 128 | Yes (general knowledge) | NAUG §11 (p.153) shows the 6850 and Serial ULA at the same addresses; NAUG §16.3.1 (pp.261-263) documents the CFS on the Master; MAME `bbcm.cpp:773-793` wires a cassette to the VLSI SERPROC |
| **Master Compact** | **No** | No cassette socket (general knowledge). NAUG §11 (pp.153-154) marks the 6850 "present only if serial expansion fitted", and §15.3 (p.240) says the optional chips must be inserted for RS232. No local document says in so many words that it has no cassette port. MAME `bbcmc.cpp:584-599` fits no cassette device. `docs/discussion/serial-architecture-review.md` already records "never" |

- **Every variant Beebium builds today has the interface.**
- **The Master Compact is where a presence axis earns its keep:** it can have serial chips fitted and still no cassette. That matches the review note's separation of cassette presence from ACIA presence.
- **A Master 128 variant would bring the VLSI part,** with its reportedly shorter DCD threshold and the bit-3 tone swap (BeebWiki).

---

## 7. Decoding audio to bits

The requirements now include loading from a WAV file and, if feasible in real time, from a live audio input. That makes the analogue side and the Serial ULA's demodulator a real component, and probably puts the tape seam at waveform level, with UEF synthesised into the same representation. This section gathers what such a design needs; it does not design it.

Additional sources:

| Tag | Source |
|---|---|
| T30963 | stardot, "Replacement Serial ULA module for the BBC Micro", https://stardot.org.uk/forums/viewtopic.php?t=30963. 12 pages at 30 posts a page; cited as page/post |
| HDL | The CPLD replica of the Serial ULA that thread produced, https://github.com/hoglet67/SerialULA, `src/serialula.v` (Verilog), `src/serialula.vhd`, `test/`, `xilinx/`. **No licence file**: read for understanding only. Nothing here is copied from it; behaviour is restated, and line numbers are given for checking |
| QB | Quadbike 2.0.5 manual, `~/Code/quadbike-2.0.5-rc7/docs/html/index.html` (anchors given), and https://beebstuff.uk/w/acorn/quadbike-web/. **Documentation only**; the source was not opened |
| BCL | BBC Cassette Loader, https://github.com/rokcoder-bbcmicro/bbc-cassette-loader, GPL-3.0-only, C#; commit c3a5a50 |

### 7.1 How the real Serial ULA demodulates

**Signal chain (Acorn documents).**

- An LM324 front end buffers, filters and squares the signal, and AC-couples a square wave of about 1.2-1.4 V peak to peak into the ULA (SM-B §3.5; SM-B+ pp.31, 51-52, 92).
- The ULA's "data separator" turns it into a receive clock and data for the ACIA. Its "high tone run-in detect" drives DCD. In cassette mode the receive clock comes from the FSK signal, not from the control register (SM-B §3.5, figure 9; SM-B+ §5.12).
- **The service test expects a symmetric signal.** It asks for high and low tones of equal amplitude, symmetric about 0 V, and a square wave with an even mark/space ratio at the limiter output. If the ratio is uneven, the fix is to reduce the playback volume (SM-B+ §9.8, pp.51-52). An asymmetric square from overdrive or DC offset is therefore a known real-world failure mode.

**The analogue front end** (T22935/p327034, p327576; HDL `test/spice/BEEBCASSETTE.txt:3-23`):

| Stage | Type | Components | Corner | Gain |
|---|---|---|---|---|
| 1 | 2nd-order Sallen-Key high-pass | 2 × 820 pF, 150 kΩ | about 1.29 kHz | about 1.27 |
| 2 | 2nd-order Sallen-Key low-pass | 2 × 8.2 kΩ, 4.7 nF | about 4.13 kHz | about 1.26 |
| 3 | High-gain inverting clipper | clamping transistors | — | about 50 |

- **The front end moves the zero crossings.** It shifts 1200 Hz by about +65° and 2400 Hz by about −5° in simulation, and by nearly 90° on the low tone in scope captures. So low-tone crossings move relative to high-tone ones, and the input stage itself rewrites the phase an offline decoder worries about.
- **Real tapes come in every phase.** Quadbike's statistics across one collection found 270° on 66 tapes, 90° on 33, 0° on 5 and 180° on 6 (T30963 P7/p456779).
- **A square wave shifted 90° fails** on both Ferranti and VLSI parts. A sine-like 90° signal still loads (T30963 P7/p456774).
- **Input threshold.**
  - The ULA input is capacitor-coupled. Its switching threshold is estimated at 1.3-1.7 V (T30963 P1/p454444).
  - The replacement module's pin 12 was measured as about ±0.5 V around 1.46 V (P3/p455924).
  - **No hysteresis figure exists for the input.** The only hysteresis reported is on the Ferranti's RC (CR) pin (P5/p456479).

**Tone discrimination and clock recovery.** The scope work (T22935/p327064-p327765) found the Ferranti and VLSI parts identical here (p327134, p327436).

- **The detector is driven by edges.** Every zero crossing, rising or falling, triggers a burst of 4 receive-clock pulses, each 1.625 µs low and 1.625 µs high.
  - The burst follows the edge by about 20-25 µs on the real part (p327068, p327226); the replica places it at 13 µs (HDL `serialula.v:179`).
  - The detector does not care about input polarity (p327068, p327159).
- **Missing-edge timeout.** If no further edge arrives within a threshold, a second burst follows.
  - Measured thresholds: about 260 µs (about 320 clocks, p327068) and about 284 µs (p327573). The replica uses 286 µs (176 ticks of 1.625 µs, HDL `serialula.v:180`).
  - A Ferranti needs a gap of about 300 µs or more to read a zero reliably (p327765).
  - Nominal half-cycles are 208 µs at 2400 Hz and 416 µs at 1200 Hz.
- **Sixteen clocks per bit for either tone.** A 2400 Hz bit has 4 half-cycles of 1 burst each. A 1200 Hz bit has 2 half-cycles of 2 bursts each. Either way the ACIA sees 16 clocks per 1200-baud bit, which suits /16. At 300 baud a bit spans four such cells, giving 64 clocks, which suits /64.
  - **The demodulator knows nothing about baud rate.** 300 and 1200 baud differ on receive only in the ACIA divider (p327120; T30963 P2/p454660).
  - That is also why tape receive offers only 300 and 1200 baud: the recovered clock is not divided by the ULA, so receive bits 5-3 have no effect. Transmit does use the ULA divider, and saving at 75, 150 and 600 baud works on real hardware, but such tapes cannot be read back (T30963 P1/p454535, P10/p459202).
- **The data decision.** RxD is decided at each edge from the gap just ended. In the replica, a long gap gives 0, two consecutive short gaps give 1, and otherwise the output holds (HDL `serialula.v:179-221`).
  - The recovered data lags the tape by about half a bit. Data goes from 0 to 1 only on the third edge of 2400 Hz (p327028, p327137).
  - The ACIA's mid-bit sample falls in the third burst, which already carries the new value.
- **Frequency tracking.** Burst spacing follows the input in 3.25 µs steps with about ±3.25 µs of jitter: about 192 µs at 2600 Hz, 208 µs at 2400 Hz and 227 µs at 2200 Hz (p327064).
  - Recovery happens edge by edge, not trained once on the leader, so it tracks wow and flutter (p327114).
  - The byte-level tolerance is the ACIA's stop-bit margin, about ±5.5%.
  - In turbo-loading experiments, high tone could be pushed far above nominal, but low tone could rise only about 2-3% before failing.
- **Edges inside a burst.** An edge that arrives during a timeout burst makes the real ULA abandon the burst and emit fewer than 4 clocks. This was seen at 1707 Hz, and with a 293 µs gap, on both chip types (p327436; T30963 P7/p456782).
  - The replica always finishes a burst, which is why it loaded some turbo tapes that real chips rejected (p327765).
  - A 90°-shifted waveform produces intermediate gaps of about 312 µs, which can add bursts (18 clocks a bit instead of 16) and push the ACIA out of step (p327470).
- **No correction at the ULA.** A dropout times out and reads as low tone; a noise spike adds an edge and a burst. Only the ACIA's framing and the MOS's CRC catch either.

**High-tone detection and DCD.**

- **Ferranti** (T22935/p327053-p328773; T30963 P4-P8):
  - The delay comes from an external RC on pin 15 (100 kΩ, or 56-100 kΩ by board, with 4.7 µF), with a diode that discharges it when the motor is off.
  - Only tone at or above about 1790 Hz charges it; 1780 Hz does not.
  - DCD fires after about 186-215 ms of high tone, independent of tone frequency. Halving R75 halves the delay.
  - **The output is a pulse one half-cycle long, aligned to input edges.** Noise on the slowly charging capacitor varies which edge starts it.
  - One cycle of low tone discharges about 90% of the capacitor (p328708). The MOS's motor-off blip of about 700 µs between blocks does the rest.
  - The threshold varies between chips. One older Ferranti needed a much shorter motor blip than another to give the same behaviour (T30963 P5/p456455-p456479).
- **VLSI** (p327165-p327257):
  - Pin 15 sits at 5 V and the RC is not fitted.
  - DCD fires after about 92.7 ms of continuous recovered 1s, which is 445 ticks of 256 clocks. That is independent of frequency (92.6, 92.7 and 92.8 ms at 2000, 2400 and 2800 Hz) and of temperature.
  - The pulse is exactly 256 clocks (208 µs) and is **not** aligned to the input.
- **Behaviour common to both** (T30963 P5-P8):
  - Motor off stops charging and forces DCD low.
  - Register writes that leave the motor on do not reset the timer, whether the value repeats (&85, &85) or changes (&85, &86).
  - With RS423 selected and the motor on, the timer keeps charging while DCD is held low, and switching to cassette does not restart it (P6/p456585, p456685; on VLSI, P7/p456751).
  - **Motor off must reset DCD.** The Revise GCE-CSE Mathematics 1 tape, which has a phantom block, needs it (P8/p457271).
- **The replica's DCD** (HDL `serialula.v:223-239`):
  - A counter ticks every 208 µs while the recovered data is 1 and the motor is on, and is cleared by a recovered 0 or by motor off.
  - DCD is a single 208 µs pulse at 962 ticks (about 200 ms, the Ferranti option) or 445 ticks (about 93 ms, the VLSI option).
  - It watches the *recovered data*, so silence after high tone does not clear it.
- **MOS sensitivity.** MOS 1.20 uses DCD only to step from "looking for carrier" to "waiting for &2A", so it is indifferent to the exact delay (T22935/p328773).
- **Title sensitivity.** In emulator tests, a delay of 50 ms or more is safe and Atic Atac fails at 40 ms (T30963 P2/p454736). A "fast DCD" that shortens leaders for turbo loading therefore risks breaking protection schemes and was proposed only as a non-default option (P1/p454535, P2/p454713).

**Ferranti against VLSI.**

- **Bit 3 tone swap.** On the VLSI part, &FE10 bit 3 in cassette mode swaps the tone sense for both load and save. The Ferranti ignores bits 5-3.
  - Titles that write such values fail on the VLSI part: BASICODE 2, Facemaker, Fortress (a framing error) and Caveman Capers (BeebWiki).
  - The thread adds Atic Atac and Video's Revenge, which write &AD (T30963 P4/p456084-p456113).
  - The replacement module therefore defaults to Ferranti behaviour, with a jumper for VLSI (P6/p456524, P11/p479867).
- **Other differences.**
  - The DCD mechanism and delay, above.
  - The two parts transmit opposite polarity (T22935/p327144), and the VLSI waveform is noisier.
- **Which machines have which.** Masters use the VLSI part. Most Model Bs examined have Ferranti parts, but a VTI-branded part has been found in an issue 7 Model B (P1/p454576, P5/p456455).

**Transmit.**

- **The waveform.** A four-level staircase with 8 slots per cycle and the peaks held for two slots. The levels measured on a Ferranti are 2.95, 3.36, 4.52 and 4.92 V at pin 27 (HDL `serialula.v:245-306`; T30963 P6/p456550). The board smooths and attenuates this to about 200 mV peak to peak at the socket.
  - The Ferranti's middle levels sit higher than an ideal sine, and gave the receiver *better* timing margin: 119-130 µs against 95 µs (P8/p457000).
- **Tone changes.** They happen at zero crossings with continuous phase. The replica re-samples TxD and RTS only every 832 µs, at a zero crossing, so each cell is one cycle of 1200 Hz or two of 2400 Hz (HDL `serialula.v:257-290`).
  - On the real chips the output lags TxD by up to one high-tone cycle (T22935/p327144).
  - Early replica saves that changed tone at a peak (a 90° error) could not be read by real ULAs (T30963 P7/p456753).
- **Silence.** RTS high, or RS423 selected, gives silence.

**What remains unknowable** (no netlist exists for either part):

- the exact missing-edge timeout (260, 284 or 286 µs) and how it is quantised;
- the origin of the 20-25 µs delay from edge to burst;
- the input threshold and hysteresis;
- the precise Ferranti high-tone criterion and its spread across chips;
- the VLSI's internal mechanism;
- board-to-board variation in the front end;
- one unexplained 16-clock burst seen on a VLSI part (p327275);
- 300 baud on noisy or wowed input, which has never been measured systematically.

**Behavioural parameters of the ULA's cassette side.** The demodulator model can be written from this table.

| Parameter | Value or rule | Source |
|---|---|---|
| Master clock | 16 MHz / 13 = 1.2308 MHz (812.5 ns); every rate is 0.16% above nominal | SM-B §3.5; HDL `serialula.v:21` |
| Front end | 2nd-order HP about 1.29 kHz, 2nd-order LP about 4.13 kHz, gain-50 clipper, AC-coupled; phase about +65° at 1200 Hz, −5° at 2400 Hz | T22935/p327034, p327576; HDL `test/spice` |
| Input threshold | Estimated 1.3-1.7 V on an AC-coupled input (replacement measured at about 1.46 V); hysteresis unknown | T30963 P1/p454444, P3/p455924 |
| Edge sensitivity | Both edges; polarity-insensitive | T22935/p327068, p327159 |
| Discrimination method | Edge-to-edge (half-cycle) timing against a single timeout; no frequency-domain filtering | T22935/p327068; HDL `serialula.v:179-221` |
| Missing-edge timeout | About 260 µs (p327068), about 284 µs (p327573); replica 286 µs; a Ferranti needs about 300 µs for a reliable 0 | T22935; HDL `serialula.v:180` |
| Receive clock (RxC) | Burst of 4 pulses (1.625 µs low and high) about 20-25 µs after each edge (13 µs in the replica), plus a second burst at the timeout; 16 per 1200-baud bit for either tone | T22935/p327064-p327226; HDL `serialula.v:192-200` |
| Edge during a burst | Real ULA abandons the burst (fewer than 4 pulses); the replica completes it | T22935/p327436, p327765; T30963 P7/p456782 |
| Data (RxD) rule | At each edge: long gap → 0; two short gaps → 1; else hold. Lags the tape by about half a bit | T22935/p327028, p327137; HDL `serialula.v:179-221` |
| Bit-cell timing | 1200 baud: 833 µs = 1 cycle of 1200 Hz or 2 of 2400 Hz. 300 baud: 4 cells (4 or 8 cycles). Rate set only by ACIA /16 or /64; receive bits 5-3 ignored in cassette mode | SM-B+ §5.12; AUG §20.6.1; T30963 P2/p454660 |
| High-tone acceptance | Ferranti: tones of about 1790 Hz or more charge the detector; 1780 Hz does not | T22935/p327070 |
| DCD assert | Ferranti: RC on pin 15, about 186-215 ms of high tone, chip- and board-dependent. VLSI: counter, 445 × 256 clocks = 92.7 ms of recovered 1s | T22935/p327136, p327165; T30963 P6/p456685; HDL `serialula.v:10-17` |
| DCD shape | Pulse, not a level. Ferranti: one input half-cycle long, edge-aligned. VLSI: 256 clocks (208 µs), not edge-aligned | T22935/p327255, p327165; HDL `serialula.v:223-239` |
| DCD release and re-arm | Fires once per run of high tone. Re-armed by low tone (one 1200 Hz cycle discharges about 90% on Ferranti; any recovered 0 clears the replica's counter) and by motor off (forces DCD low and clears the timer). Not reset by writes that keep the motor on, nor by switching RS423 to cassette | T22935/p328708; T30963 P5/p456479, P6/p456685, P7/p456751, P8/p457271 |
| DCD in RS423 mode | Forced low; the timer keeps charging if the motor is on | AUG §20.7.3; T30963 P6/p456585 |
| VLSI bit 3 | In cassette mode, inverts the tone sense on receive and transmit; Ferranti ignores it | BeebWiki; T30963 P4/p456084-p456113; HDL `serialula.v:214, 266` |
| Transmit tones | 2403.8 Hz and 1201.9 Hz from the master clock; TxD sampled at 832 µs cell boundaries at a zero crossing; phase-continuous; RTS high gives silence | SM-B §3.5; HDL `serialula.v:257-290`; T22935/p327144 |
| Transmit levels | 4-level staircase, 8 slots per cycle, peaks held two slots; Ferranti 2.95 / 3.36 / 4.52 / 4.92 V at pin 27; about 200 mV peak to peak at the socket | HDL `serialula.v:245-255`; T30963 P6/p456550; SM-B+ §3.5 |
| Transmit polarity | Opposite between Ferranti and VLSI | T22935/p327144 |
| Reading &FE10 | Latches the data bus as a write (no R/W pin), so in practice a write of &FE. Stated in the replica's source and by emulators, not measured | HDL `serialula.v:102`; beebjit `serial_ula.c:154-166` |
| Motor | Bit 7 drives the relay and the LED (pin 11) directly | AUG §20.9.4; T30963 P11/p478134 |

### 7.2 Quadbike (from its documentation only)

Quadbike is an offline archival decoder whose output is TIBET, CSW or UEF. All references are to QB anchors.

- **Input** (`#H_Record_Settings`, `#H_Preparation`, `#H_Pre-processing`):
  - Lossless WAV, AIFF or FLAC, at 22.05 kHz or above. 44.1 or 48 kHz is recommended, and 24-bit is preferred over 16-bit for tapes with wide dynamic range.
  - The user chooses one channel; the channels are never combined.
  - Record at about half of full scale, because clipping inside a block ruins the capture.
  - Pre-filtering is discouraged. An optional 400-3200 Hz band-pass and normalisation rarely help.
- **No zero-crossing detector** (`#H_Goertzel_s_Algorithm`, `#H_Signal`):
  - A sliding Goertzel filter, about 37 samples at 44.1 kHz (one 1200 Hz period, scaled by measured speed), gives power at each tone.
  - The unit of decoding is the "atom": half a 1200 Hz cycle or one 2400 Hz cycle, both about 1/2400 s.
  - A high-tone power scaler can be tuned, or searched for in "scan mode".
- **Segmentation and level** (`#H_Spans`):
  - Spans of silence, leader and data are separated with 0.05 s of hysteresis.
  - All level thresholds are fractions of the loudest Goertzel power in the whole file, which is its only level handling. Version 2.0.3 lowered the default threshold so tapes that fade still decode.
  - Leader is not decoded but replaced with synthetic 1s.
- **Speed** (`#H_Tape_Speed`):
  - One speed is measured per span, by sweeping 1803-3004 Hz.
  - There is no explicit wow or flutter tracking within a span. Tracking comes only from the sync method.
- **Sync modes** (`#H_Synchronisation_Methods`):
  - **freq**, the default and phase-agnostic: fit whole atoms between tone transitions.
  - **pll**: square the signal to recover a 2400 Hz carrier, then lock a software PLL. For short bursts after silence it runs backwards.
  - **walk**: use waveform peaks and valleys, and interpolate across gaps longer than 1.6 atoms.
  - A ±2-sample shift search in walk and PLL modes.
- **Phase and polarity** (`#H_A_Note_on_Phase`, `#H_Phase_Detection`, `#H_Polarity_Reversals`):
  - Four phase values (0, 90, 180, 270), with inversion treated as part of phase.
  - Detected globally (the default), per block, or forced.
  - An odd number of low-tone half-cycles flips polarity; CSW output corrects for it.
- **Errors** (`#H_Odd_Runs_and_Fuzzy_Bits`, `#H_TIBET_1200_Baud`, `#H_TIBET_300_Baud`):
  - The quality metric is the count of "odd runs" of atoms.
  - Fixes: flip an atom, insert an atom, or force even counts.
  - The TIBET rule on a fuzzy bit is to drop its first atom and resynchronise. Majority voting over a 300-baud bit's 8 atoms is explicitly forbidden, because it risks a persistent one-atom misalignment.
  - No framing or CRC work is done in the decoder itself.
- **300 baud and framing.** Quadbike cannot detect either. Its manual insists that in an emulator such hints must never reprogram the ACIA; that is the loader's job (`#H_TIBET_Notes_on_Hints`).
- **Formats** (`#H_Problems_with_CSW`, `#H_Problems_with_UEF`):
  - It calls CSW "largely worthless" for archiving: no phase, silence only as long pulses, and fixed-threshold readers that fail at phase 90 or 270 and at non-44.1 kHz rates.
  - UEF &0100 and &0104 cannot carry mid-block framing changes.
  - &0102 is poorly supported and misrepresents 300 baud.
  - &0114 is the only general cycle-level chunk.
- **What it means for us.** Quadbike's methods are offline and non-causal (whole-file normalisation, per-span speed, phase search, running the PLL backwards), so a real-time ULA model cannot use them. Its atom model is also coarser than the ULA's half-cycle granularity.

### 7.3 BBC Cassette Loader

A Windows Forms application (.NET 4.7.2, NAudio) for recovering standard 1200-baud cassette files. GPL-3.0-only, so ideas may be taken but not code. The repository has one commit and one release (v1.0.0), with no issues.

- **Input:** WAV files, or live line-in through WinMM at 48 kHz, 16-bit mono (`src/Audio/ToneHandler.cs:86-93`). It can record the line-in to WAV at the same time.
- **Output:** recovered files with `.inf` metadata, DFS or ADFS disc images, and a CSW rebuilt from the decoded bytes. There is no UEF output and no 300-baud support.
- **Pipeline:**
  1. Resample to 48 kHz.
  2. Filter: a 300 Hz high-pass (its only DC removal) and a 6 kHz low-pass. There is no automatic level control.
  3. Classify each peak-to-trough half-cycle: 1600-3000 Hz is high tone, 800-1600 Hz low tone. This timing is used only to find leader and the start of data.
  4. Lock on after more than 1200 cycles (0.5 s) of carrier followed by a low half-cycle, and measure the bit length once from the carrier as a whole number of samples.
  5. Decide each bit by correlating its first half with its second half: negative is 0, positive is 1. The magnitude is kept as a per-bit confidence.
  6. Track the bit clock by snapping to the nearest trough, at most ±3 samples per bit.
  7. Any start-bit, stop-bit or CRC error returns to hunting for carrier.
- **Robustness measures:**
  - An optional CRC repair flips the 8 least-confident bits, singly and in pairs, and accepts a fix only if exactly one candidate passes.
  - An optional recovery mode makes up to eight passes with different settings (inverted signal, other filters, other alignment), keeping only blocks that pass their CRC.
  - Blocks from several recordings of the same tape can be merged.
- **Tests and claims.** About 60 checks in a hand-written runner, on synthetic sine waves with noise, inversion, baseline wander and ±20% constant speed error. There are tools for comparing results on a private set of real recordings, but no recordings ship and no accuracy figures are published.
- **Assessment:**
  - **Worth learning from:**
    - deciding bits by half-bit correlation, which ignores amplitude, polarity and DC;
    - measuring speed from the leader;
    - per-bit confidence;
    - CRC repair that accepts only a unique fix;
    - merging blocks across recordings.
  - **Weak:**
    - polarity is handled by whole extra passes;
    - the bit clock cannot follow changing speed, so wow and flutter are untested;
    - any framing error drops back to carrier hunting;
    - one large class mixes signal processing with device code.
  - **Apparent defects:**
    - its CSW writer labels the data as RLE but zlib-compresses it;
    - it expects a data CRC after a zero-length block, which MOS 1.20 does not write (section 2.3).
  - It is a decoder for archiving, not a ULA model. It is useful at most as an independent second opinion on recordings, run outside the repository.

### 7.4 CSW, WAV and live audio in the other emulators

| Emulator | CSW | WAV or raw audio as a tape | Live audio capture |
|---|---|---|---|
| b2 | No | No | No; the audio device is opened for playback only (`src/b2/b2.cpp:960-962`; `wip/tom` `:1162-1167`) |
| BeebEm | v2 Z-RLE, pulse-classified (section 3.2) | No; loader accepts `*.uef;*.csw` only (`Src/BeebWinIo.cpp:369-404`) | No; the Mac build's AVAudioEngine is output-only (`BeebEm-Sound.swift:93-157`) |
| B-Em | v2 assumed, pulse-classified (section 3.3) | No; loader table is UEF and CSW (`src/tape.c:23-24`); WAV appears only for sound recording and drive-noise samples | No |
| beebjit | v2, cycle-sum classified (section 3.4) | No (`tape.c:154-157`) | No |
| jsbeeb | No | No | Microphone via `getUserMedia`, but only as an envelope level for the analogue port, not tape (`src/microphone-input.js:54-124`) |
| MAME | Yes | **Yes**: WAV, FLAC and AIFF are cassette formats for every system, the BBC included (`src/lib/formats/cassimg.h:227-232`) | Generic sound input exists in MAME's OS layer, but no Acorn driver wires it to the cassette (no microphone device in `src/mame/acorn/`) |

- **No local emulator renders UEF into a waveform for decoding.**
  - B-Em's `tapenoise.c` synthesises tone for the speaker only.
  - BeebEm's tape audio (`Sound.cpp:390-427`) is likewise output only.
  - b2 has no tape.
- **MAME's waveform path.** MAME is the only reference that renders UEF into a waveform and decodes it (section 7.7).

### 7.5 Real-time feasibility

- **Rates.**
  - The emulated clock is 2 MHz (`docs/clock-architecture.md`), so one audio sample is 45.35 cycles at 44.1 kHz or 41.67 at 48 kHz. The non-integer ratio needs a fractional accumulator, as the SN76489's 48 kHz output already uses (`docs/sound-subsystem.md`).
  - Each 200 Hz pacing tick (10,000 cycles) covers 220-240 samples.
- **Cost per sample.** A zero-crossing or period decoder does:
  - a DC-blocking one-pole high-pass;
  - optionally one biquad;
  - a sign test with hysteresis;
  - a counter;
  - on each of the 2400-4800 crossings a second, a few comparisons.

  That is about 10-30 scalar operations, or 20-50 host cycles, per sample. Against `docs/performance.md`, it is about 0.1-0.3% of the emulation thread's per-sample budget on a Pi 5 or a Pi 400. By comparison, the SN76489 alone shows at 3-5% in the profiles.
- **Precedents for per-sample work on the emulation thread:**
  - MAME runs exactly such a decoder per 44.1 kHz sample on its emulation timer (`acorn_serproc.cpp:96`, 146-254).
  - Beebium's Music 5000 runs a fourth-order filter and a resampler per sample on the emulation thread (`docs/music-5000.md`).
- **Three possible placements:**
  1. **Per sample on the emulation thread, in emulated time** (MAME's shape). Deterministic for a file. Live input needs a lock-free sample buffer.
  2. **On a separate thread, producing half-cycle durations or bits.** The emulation thread consumes events, as it would for UEF or CSW. A whole WAV can be decoded ahead: 10 minutes at 48 kHz is about 2.9 million crossings.
  3. **On the client** (section 7.6).
- **No locks on the per-cycle path.** Nothing on the per-cycle path may take a lock (`docs/emulation-thread-ownership.md`). Two existing patterns cross the thread boundary:
  - the SPSC `OutputQueue` (`AudioBuffer.hpp`), which is generic and could carry data inward;
  - the host-serial reader thread feeding a queue that the ULA drains (`src/extensions/host-serial/HostSerialEndpoint.hpp:41-49`).

**WAV file against live stream:**

| | WAV file | Live stream |
|---|---|---|
| Time base | Sample index maps to emulated time; works at any speed, paused or stepped; deterministic | Arrives at the audio device's real-time rate, which the emulator must keep up with |
| Read-ahead and seek | Whole file available; rewind and motor control move or stop the position | None beyond what has arrived; rewinding is done by hand on the deck |
| Motor relay | Pauses the file | Cannot stop the user's deck (unless the deck's remote is wired to real hardware); samples keep arriving |
| Pause, debugger, skipped ticks | Tape stops with emulated time | Samples keep arriving; backlog, or discontinuity on resume |
| Speed other than 1x | Fine | Above 1x the buffer drains and the guest sees gaps; below 1x it grows without bound |

- **Clock drift.** Consumer audio clocks are good to tens of ppm, up to about 100. At 100 ppm, 48 kHz drifts about 30 ms over a 5-minute load. A period decoder is unaffected, since the scale error of about 1e-4 is far inside the 2:1 tone ratio; only the buffer level changes. The pacing clock's skipped ticks (`docs/clock-architecture.md`) are a larger and burstier source of mismatch on a loaded host such as a Pi 400 running the client.
- **Latency.** Typical input buffering:

  | Route | Typical buffering |
  |---|---|
  | CoreAudio | 512 frames, about 11 ms |
  | WASAPI shared mode | 10 ms |
  | PipeWire | 1024 frames at 48 kHz, about 21 ms |
  | PulseAudio | `fragsize`, typically tens of ms |
  | Over gRPC from a client | add chunking and network time |

  Beebium's playback already pre-rolls 120-480 ms (`clients/linux/src/audio/PreRoll.hpp`).
- **What the Beeb tolerates.**
  - The MOS waits for carrier with no timeout (section 2.5), so start-up latency and an initial buffer fill are harmless.
  - **An underrun mid-block is not.** A missing stretch looks like a long half-cycle, which reads as low tone; MAME's decoder resets after 27 samples with no crossing. The filing system then reports `Data?` or `Block?`.
  - A buffer that never empties during a block matters more than its absolute latency.

### 7.6 Host audio input

**From a C++ server:**

- **macOS.**
  - **APIs:** AUHAL input (`kAudioUnitSubType_HALOutput` with input enabled, Apple TN2091), Audio Queue `AudioQueueNewInput`, or AVAudioEngine's input-node tap.
  - **Permission:** microphone access is TCC-protected. TCC charges a child process's access to its *responsible process*:
    - a server the macOS app starts with `Process` (`clients/macos/Beebium/Beebium/Presets/PresetManager.swift:683-696`) is charged to Beebium.app;
    - one started from Terminal is charged to Terminal;
    - one started by launchd or over SSH has nothing to prompt and is denied.
  - **What Beebium.app lacks for this:** an `NSMicrophoneUsageDescription` in its Info.plist, and the `com.apple.security.device.audio-input` entitlement that the Hardened Runtime requires. Its release signing adds no entitlements (`.github/workflows/macos-app.yml:282-294`).
- **Linux.**
  - **APIs:** ALSA (`snd_pcm_open` for capture; LGPL), PulseAudio (simple or async API; LGPL; also served by pipewire-pulse), or PipeWire (`pw_stream` input; MIT).
  - **Pi images:** Raspberry Pi OS desktop runs PipeWire, while Lite has no sound server and only ALSA. The Pi has no built-in audio input, so capture needs a USB interface.
  - **Session audio:** a server outside a user session (a system service, or over SSH) may not reach PipeWire or PulseAudio.
  - **Packaging:** the static server bundle already `dlopen`s Avahi (`docs/packaging.md`), and the same works for the audio libraries.
- **Windows.**
  - **API:** WASAPI capture (`IAudioClient`, `IAudioCaptureClient`; shared mode typically 48 kHz float, 10 ms period).
  - **Permission:** one "Let desktop apps access your microphone" switch covers all unpackaged applications, which cannot be blocked one by one.
- **Portable libraries** (local vcpkg ports):

  | Library | Version | Licence | Notes |
  |---|---|---|---|
  | miniaudio | 0.11.25 | Unlicense or MIT-0 | Runtime-links the Linux backends; includes a WAV decoder and a resampler |
  | PortAudio | 19.7 | MIT | No native PulseAudio host API |
  | RtAudio | 6.0.1 | MIT-style | |
  | SDL2 / SDL3 | | zlib | |
  | dr_wav | | Unlicense or MIT-0 | WAV reading only |
  | libsndfile | | LGPL | File reading only |

  The server today links no audio library: `vcpkg.json` has only gRPC and protobuf.

**What Beebium already does for audio output:**

- **Server.** It never opens an audio device. The emulation thread writes 48 kHz frames of four 32-bit fields into `AudioBuffer`, a 1-second SPSC ring. `AudioFanOut` feeds each subscriber's queue.
- **The stream.** `AudioService.SubscribeAudio` streams `AudioChunk { sequence, sample_count, cycle_count (reserved), samples }`, about 768 kB/s per subscriber (`src/service/proto/audio.proto`).
- **macOS client.** Plays through an AVAudioEngine `AVAudioSourceNode` (`AudioEngine.swift`).
- **Linux client.** Plays through `QAudioSink` with a 40 ms device buffer and a 120-480 ms adaptive pre-roll (`clients/linux/src/app/AudioOutput.cpp:125-160`).

**Capturing on the client and streaming to the server:**

- **Capture APIs.** Qt's `QAudioSource` (the Linux client already links Qt Multimedia), or an input tap on the AVAudioEngine the macOS client already owns.
- **Permissions move to the client.** On macOS the app needs the usage string and entitlement either way, since a spawned server is charged to it.
- **This is the only route that captures the user's microphone when the server runs on another host.**
- **Bandwidth:**

  | What is sent | Rate |
  |---|---|
  | Mono 16-bit PCM at 44.1 kHz | 88.2 kB/s |
  | Mono 16-bit PCM at 48 kHz | 96 kB/s |
  | Half-cycle durations (decoded on the client) | a few kB/s |
  | Bits | about 150 B/s |

  Even the PCM rate is an eighth of one audio output stream.
- **Precedent: none.** Every streaming RPC in the protos runs server to client. Client-to-server data today travels in unary calls: keyboard `KeyDown`/`KeyUp`, `rpc-serial` `Send`, `ExtensionRpc.Invoke`.
- **Server-side cost.** The server uses gRPC's synchronous API throughout, so a client-streaming RPC (`ServerReader`) holds one worker thread for the life of the stream, as each output stream already does.

### 7.7 MAME's waveform path and PlayUEF

**MAME has one waveform path, but its decoder is not a ULA model.** The details below were read in the local MAME checkout; it is BSD-3 and GPL-2, so it was read, not copied.

- **Every source is reduced to samples.**
  - **UEF** is rendered once, for the whole tape, at load time, into **4800 Hz** samples (`src/lib/formats/uef_cas.cpp:31`; `cassimg.cpp:789-910`).
  - **CSW** is expanded at its own sample rate, keeping its initial polarity (`csw_cas.cpp:90, 100, 209`).
  - **WAV** goes into the same store.

  The driver reads the store through `cassette_image_device::input()`, which takes the nearest sample with no interpolation (`src/devices/imagedev/cassette.cpp:142`; `cassimg.cpp:172`). The position advances only while the motor is on.
- **UEF rendering:**
  - A square wave at ±32767, silence at 0, with fixed phase: every cycle starts low. Each bit is 4 samples: two 2400 Hz cycles for 1, one 1200 Hz cycle for 0.
  - Handled: &0100, &0110, &0112 (n/2400 s), &0116, and &0117 at 300 or 1200.
  - Skipped: &0111, &0113, &0114 and &0115.
  - &0104 is played as 8N1, with its 3-byte format header played as data.
  - &0102 counts bits wrongly (length × 10) and plays its count byte as data.
  - Gzip is inflated whole into a file-level buffer during the size pass; the source comment admits this is crude. Reading the code suggests a long 300-baud tape might overrun that buffer; this was not run.
- **The decoder** (`src/mame/acorn/acorn_serproc.cpp:146-255`):
  - It polls at 44.1 kHz in emulated time and counts samples between sign changes.
  - Two half-periods summing to at least 31 samples give a 0. Four summing to at most 41 give a 1. A half-period over 27 samples resets the state; the source calls that "a hack" (`:200`).
  - Silence has no crossings, so a gap is noticed only at the next edge.
  - Each decoded bit fires **16 receive-clock pulses at once**, so the 6850's /16 or /64 does the rate work. The comment there is "Exact details are unknown, faking it" (`:137`).
  - DCD rises after more than 100 consecutive 1 cells (about 84 ms) and is held as a **level** until a 0 or a gap. That contradicts the pulse measured in section 7.1.
  - Recording emits 9- and 18-sample half-periods, so a bit lasts about 816 µs instead of 833 µs, regardless of the transmit clock.
  - The header TODO is to rewrite cassette handling and lower the sample rate to 4800 Hz (`:11-12`).
  - The decoding dates from 2013. It was split into this device file in 2025.
- **The Electron** shares only the image library. Its own decoder (`electron_ula.cpp:298-370`) polls at 4800 Hz and pattern-matches the 4-sample cells the UEF renderer makes, so it recognises MAME's UEF output rather than demodulating audio.
- **Verdict.** MAME shows the single-waveform architecture working end to end, but neither its renderer nor its decoder is faithful to the hardware. It is weak as a cross-check.

**PlayUEF.** https://github.com/8bitkick/PlayUEF, GPL-3.0 (confirmed from its LICENSE), active as of November 2026. A browser and Node tool that renders UEF to audio for loading into a real machine. It can be run separately as a cross-check; none of its code can be used.

- **Rendering.** A 48 kHz sine built from whole-cycle pieces, each restarting at a fixed phase (180° by default), with lengths rounded down.
- **Chunks:**
  - &0100 is rendered as 8N1.
  - &0104 honours data bits, parity and stop bits, with the MakeUEF-before-2.4 parity fix.
  - &0110 and &0111 are handled, with an adjustable carrier length.
  - &0114 is replaced by carrier.
  - &0112 comes out 4× longer than the specification intends.
  - &0102, &0113, &0115 and &0117 are ignored on master.
- **Options:** `LOW`, `HIGH`, `PHASE`, `CARRIER`, `STOPBIT` and `TURBO`.
- **README against code.** The README claims support for &0113-&0115 and names parameters the code does not read. The &0113-&0115 support was added in commit 96127344 and reverted in 54623cf6 (June 2026). 300-baud support exists only on a 2018 `develop` branch.
- **As a cross-check.** It is good for whether a synthesised waveform carries the right bytes for &0100, &0104, &0110 and &0111, since it is reported to load on real BBC and Electron machines. It is not good for gap or phase timing, or for the chunks it ignores.

---

## Open questions for the specification

1. **Level of modelling and the seam.** With WAV and live audio in scope, the choice is where the single path begins:
   - **Samples.** The ULA model runs a front end and an edge detector, as MAME does.
   - **Half-cycle durations or edge times,** in emulated time. The front end and squarer sit outside the ULA, and the ULA model is purely the digital edge-timing logic of section 7.1, as the replica is.
   - **Something coarser.**

   Bytes injected into RDR (BeebEm, B-Em, jsbeeb) would bypass Beebium's bit-level ACIA and lose framing errors, 8N2 and overrun. Bits per 1200-baud bit time (beebjit) would bypass the demodulator.
2. **The ACIA divider.** `Mc6850` currently treats /16 and /64 as informational, and `SerialUla` takes the bit period from the ULA receive field. Cassette needs the divider to choose 300 against 1200. Is the right fix to make the ACIA divide for real (as b2 now does)? If so, should RS423 move to the same clock model at the same time, with ULA clock × ACIA divide, rather than keep a separate path?
3. **DCD shape and threshold.**
   - Ferranti behaviour is a pulse one half-cycle long, aligned to an edge, after about 186-215 ms. VLSI behaviour is a 208 µs pulse after 92.7 ms, not aligned (section 7.1). Model which, and how exactly?
   - The replica's counter approach (962 or 445 ticks of 208 µs on the recovered data, cleared by a 0 or by motor off) is a ready-made rule. The RC model, with a charging time, about 90% discharge per low cycle and a 1790 Hz acceptance threshold, is more faithful to Ferranti but less well characterised.
   - Should the threshold be per variant?
4. **6850 DCD latch.** The datasheet says status bit 2 stays set until a status read followed by a data read; real-hardware measurement (beebjit) says a data read alone clears it. `Mc6850` currently has no latch at all, so its status bit just follows the pin. Which rule do we adopt? And do we implement "/DCD high resets the receiver and hides RDRF" (datasheet), which beebjit leaves as a TODO?
5. **Detector re-arm.** What is known:
   - motor off must re-arm it (the Revise Maths tape needs it);
   - one low cycle mostly discharges a Ferranti part;
   - writes that keep the motor on do not re-arm it;
   - switching RS423 to cassette does not restart the timer.

   How silence with the motor running behaves is not known: the replica holds the count, emulators clear it. Which rule do we adopt? Uridium, Atic Atac, StarDrifter and the B-Em-TOH DCD tests are the test cases.
6. **Reading &FE10.** Should a read act as a write of &FE, as every reference emulator does? It is undocumented by Acorn and changes Beebium's current open-bus behaviour, including for RS423 users.
7. **ACIA master reset.** beebjit clears the whole control register on master reset (Frak needs it), against the datasheet. Beebium keeps the written value. Do we follow the measurement?
8. **Transmit with no recording.** What happens to bytes the ACIA sends in cassette mode when no tape is recording, or when the tape is write-protected? Real hardware clocks them out at baud rate. Pro Boxing Simulator needs TDRE to come back.
9. **Recording format and placement.**
   - Record into UEF (&0100 plus &0110/&0112 at the true ACIA rate and format, with &0117 or &0104 for non-8N1) or CSW?
   - Does recording overwrite from the current position, as a real tape does, or append, as BeebEm does?
   - Is a recorded image written back on eject or continuously, given the disc write-back precedent?
10. **Gaps on record.** RTS high gives silence. Should silence be recorded as &0112/&0116 gaps, and the motor-off periods dropped?
11. **&0114 security cycles.** No reference implements them, and the bit order is ambiguous between the two spec drafts. Support them, and if so in which order? Is a test image available that uses them?
12. **&0113 against &0117.** Draft 25 defines &0113 as a baud change; draft 28 defines it as a base-frequency change and adds &0117 for the 300-baud encoding. Which meaning do we implement for &0113?
13. **&0104 parity and the MakeUEF bug.** Honour &0104 formats exactly, including beebjit's MakeUEF-before-2.4 parity swap keyed on the &0000 origin string?
14. **CSW scope.** Read v1 and v2, both compressions, any sample rate, and initial polarity? Decode with cycle-sum classification and phase lock (as beebjit does)? Write CSW, or only read it?
15. **Mid-tape metadata.** Draft 28 says chunks like &0113 apply when playback reaches them. Do &0120 position markers and &0131 side starts appear in the user-visible tape position?
16. **Tape position model.** What is "position" in the service and UI: emulated seconds from the start, a chunk index, or a block? Is fast-forward and rewind a seek (instant) or a wind at a modelled speed? Is the block catalogue built by an offline scan of standard headers (like B-Em and BeebEm), and what is shown for non-standard blocks?
17. **End of tape.** Stop (as beebjit does) or loop (as B-Em does)? Stopping is what a real tape does.
18. **Play and the motor.** Is Play a separate user action (the deck's PLAY key) with the motor relay gating it, or does a mounted tape play whenever the motor runs (BeebEm's default)? The first is faithful; the second is what users of other emulators expect.
19. **Motor-off granularity.** When the MOS blips the motor for about 1 ms at every header, does the tape stop for that millisecond? On real hardware a deck's capstan barely moves, but the detector re-arms. Do we model a deck's spin-up and spin-down, or treat the motor as instantaneous?
20. **Motor LED.** The motor LED is driven by the ULA bit, not the deck. Should the indicator follow bit 7 directly even with no tape mounted, and should an anti-flicker hold-off be applied in the client rather than the server?
21. **Savestates and saved machines.** Should a saved machine carry the mounted image's path and the tape position, as no reference does? Should a modified (recorded) image be written into the machine directory?
22. **Presence axis.** Does cassette presence live in the hardware policy (a compile-time trait, so a future Master Compact has no cassette) or as a runtime flag reported by a service? How does it relate to the ACIA presence axis in `serial-architecture-review.md`?
23. **Ferranti against VLSI.** Should the ULA model be parameterised now for a future Master 128, or be Ferranti-only until that variant exists? The parameters would be the DCD mechanism, the bit-3 tone swap and transmit polarity. Note that at least one Model B has been found with a non-Ferranti part.
24. **Test corpus.** Three sources exist:
   - beebjit's 18 documented commercial tapes;
   - the B-Em-TOH suite of 327 files, with DCD, TDRE and save tests plus about 30 protected games;
   - the project's own WAV corpus.

   None can be committed. Do we fetch them by SHA-256 at test time into a cache (skipping when absent), reference them in place, keep them as a manual corpus, or rely only on generated tapes plus the BeebEm-recorded `Test.uef` (GPL)?
25. **Where the UEF/CSW writer for tests lives.** In this repo as test support, or contributed to oaknut alongside oaknut-romfs's block codec?
26. **`docs/serial-acia.md`.** It says the MOS programs the ACIA with &15, which is /16, but this survey finds MOS 1.20 writes &15 only during cassette SAVE setup. The idle value is &56 (/64), and RS423 uses /64. Its RS423 statement should be checked against DIS when the ACIA divide is reworked.
27. **UEF into the waveform path.** If UEF is synthesised into the same representation as audio, what shape do we use?
   - Ideal square half-cycles, which give exact edges.
   - The ULA's own transmit staircase.
   - A sine passed through a model of the front end, which shifts low-tone crossings by about 65°.

   The first makes UEF loading insensitive to phase; the last reproduces real-machine sensitivity to phase, which some protected tapes may depend on. MAME and PlayUEF (section 7.7) are cross-checks.
28. **Edge-to-burst details.** Do we model the 20-25 µs delay from edge to burst and the real chip's abandoned burst when an edge arrives mid-burst? The replica completes the burst and is known to accept signals real chips reject.
29. **Which missing-edge timeout:** 260, 284 or 286 µs? Is it worth a measurement campaign on a real machine with a known signal generator, for example with the DCD-test WAVs?
30. **The analogue front end.** For WAV and live input, how much do we model?
   - None: a zero crossing about the mean.
   - DC blocking plus hysteresis.
   - The 1.29 kHz and 4.13 kHz Sallen-Key pair and a clipper.

   The corpus's low-level take (section 5.6) defeats a naive crossing detector, and the service manual's symmetric-square test implies asymmetry is a real failure.
31. **Channel selection and level** for WAV: auto-pick the louder channel, or let the user choose? The corpus records left. Normalise or not?
32. **Where live capture happens.**
    - In the server through a portable library such as miniaudio, with macOS TCC charged to the parent app.
    - In the client, streamed to the server as a client-streaming RPC (no precedent in the protos; one sync worker thread per stream).
    - Decoded on the client and sent as edges or bits.

    The client route is the only one that works with a remote server.
33. **Live-stream timing policy.** Live audio cannot pause, rewind, or run at other than 1x. What does the emulator do when the debugger pauses, when pacing skips ticks, or when speed is not 1x mid-load? Drop, buffer, or forbid? The Beeb tolerates start-up latency but not an underrun mid-block.
34. **Motor relay with live input.** The relay cannot stop the user's deck. Do we gate the samples with the relay, which is faithful (a remote-controlled deck would stop), or ignore it?
35. **WAV recording.** Is SAVE to WAV wanted? It is the mirror image of WAV load, and the replica's measured staircase levels describe the real output.
