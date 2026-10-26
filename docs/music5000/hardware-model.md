# Music 5000: the hardware model, from the original schematic

A register-transfer description of what the Music 5000's TTL does. It is
written so that the emulation can be built from this document alone.

**Source.** "HT Music Synthesiser, Issue 1", pages 1 to 3, (c) 1984
Hybrid Technology Ltd, signed Chris J F Jordan. This is the hand-placed
`Hybrid Technology HT Music Synthesiser - Schematic.pdf` in this
directory. It is not committed; Rob placed it by hand. The pages are
drawn rotated 90 degrees.

- **Page 1:** the 1 MHz bus interface, the page register, the main RAM
  and its address path, and the modulation (INDEX) logic.
- **Page 2:** the clock and sequencer, the adder, the phase RAM, and the
  DAC drive with its stereo chopper.
- **Page 3:** the analogue output stages, the power supply and the gate
  legend.

**Conventions.** Every statement cites `p<page>` and the ICs and pins it
rests on. "Slot" means one cycle of the 3-bit instruction counter, which
is one period of phi1 (166.7 ns). Signals written with a trailing `_n`
are active low, as drawn with an overbar on the schematic. Where
the schematic leaves something unsettled the text says so, marks it
**OPEN**, and gives what the FPGA reconstruction and BeebEm each assume.
`hoglet67/Music5000` is CC BY-NC-SA and is cited, never transcribed;
BeebEm is independent of the Beech/B-Em family. The OPEN items are
collected at the end.

**The gate legend (p3).**

| Symbol | Part |
|--------|------|
| Triangle | 1/6 74F04 (so IC5 is a 74F04) |
| AND | 1/4 74LS08 |
| NAND | 1/4 74LS00 |
| OR | 1/4 74LS32 |
| XOR | 1/4 74LS86 |

The schematic fixes the device types like this: IC2, IC8 and IC9 are
AND gates; IC14 is NAND; IC4, IC30 and IC35 are OR; IC15 is XOR.

## How the disagreements in the survey are settled

`emulator-survey.md` lists the behaviours on which the existing
implementations disagree. The schematic settles them as follows.

| Behaviour | The hardware | Section |
|-----------|--------------|---------|
| Reads | The board never drives the BBC data bus: write-only. A read of `&FCFF` still clocks the page register. | 1 |
| Disable bit (freq lo bit 0) | Forces the adder's phase input to zero in the three phase slots, so the phase is loaded with the frequency. It is not zeroed. | 3 |
| Freq lo width | All 8 bits reach the adder. | 3 |
| Amplitude | 8-bit add in the log domain; output is gated off when the sign bit does not flip. | 4 |
| Amplitude timing | Latched through the phase RAM, and changes only at a phase wrap. | 4 |
| Modulation pairing | Register column r modulates column r+1 within each group of 8, with 7 wrapping to 0 and 15 to 8. The emulators' i to i+1 is right except at those two wraps. | 5 |
| Stereo | Time-sliced over 6 slots per channel; equivalent to the weights 6/6..1/6 used by the emulators. | 6 |
| Output filter | 4th order: a pole pair at 3215 Hz (Q 0.667) plus real poles at 3215 Hz and 7717 Hz. -3 dB at 2.1 kHz, -6 dB at 2.9 kHz. | 7, appendix |

## 1. Address decoding and the host interface (p1)

### 1.1 The JIM paging register, `&FCFF`

- **The latch.** IC31, a 74LS173A 4-bit register with 3-state outputs,
  is the page register. CK (pin 7) is the bus's 1MHzE. Its two data
  enables are G1_n (pin 9), driven by IC33, and G2_n (pin 10), driven
  by NPGFC from the bus connector.
  - IC33 is a 74LS30 8-input NAND. Its inputs (pins 1, 2, 3, 4, 5, 6,
    11, 12) are bus address lines A0-A7 (connector pins 27-34), so its
    output is low only for address `&xxFF`.
  - IC31 therefore loads on a rising edge of 1MHzE when the access is
    to `&FCFF`.
- **R/W is not part of the decode.** A *read* of `&FCFF` also loads the
  register, from whatever the floating bus holds (see 1.4).
- **What is stored.**
  - D1 (pin 14) is a single "not my page" bit, formed by gates. IC35
    pins 9/10 to 8 ORs D7 and D6. IC14 pins 1/2 to 3 NANDs D5 and D4.
    IC35 pins 4/5 to 6 ORs the two results into D1. So D1 = 0 exactly
    when the upper nibble written is `0011`, i.e. 3.
  - D2-D4 (pins 13, 12, 11) take bus D3, D2 and D1.
  - **Bus D0 is not connected. Page bit 0 is genuinely ignored.**
- **Outputs.** Q1 (pin 3) is WRG_n, the write gate. Q2-Q4 (pins 4, 5,
  6) drive the internal high-address lines A10, A9 and A8. Both output
  enables, M and N (pins 1, 2), are driven by IC30 pin 8, which is
  OR(PROC_n, phi1). The outputs therefore drive only during the
  host-transfer slot (1.3), and only in its phi1-low half.
- **No reset.** IC31's CLR (pin 15) is grounded, so the page register is
  never cleared and holds a random value after power-on.
- **Pull-ups.** A10, A9, A8 and WRG_n each have a 1K0 pull-up to +5 V
  (R2-R5, p1). When nothing drives them they read 1: high address bits
  `111`, and no host write.

### 1.2 JIM writes

- **Write detection.** IC35 pins 13/12 to 11 ORs R/W (connector pin 2)
  with NPGFD (pin 12). The output is low only for a write to page `&FD`.
  It feeds the D input of IC34a (74LS74, pin 12), which is clocked by
  1MHzE (pin 11). Its Q_n output (pin 8) is **SEL**, which rises at the
  1MHzE edge of a JIM write.
- **Latching the write.** SEL clocks two 74LS374 latches:
  - IC29 (CK pin 11) latches the bus address A7-A0 (connector pins
    34-27).
  - IC32 (CK pin 11) latches the bus data D7-D0 (connector pins 25-18).

  Neither latch drives anything until the host-transfer slot. IC29's
  OE_n (pin 1) is on the same OR(PROC_n, phi1) net as IC31's enables;
  IC32's OE_n is IC30 pin 3, OR(phi1, WR_n).
- **The request flip-flop.** IC34a's Q output (pin 9) clocks IC34b
  (pin 3), whose D input (pin 2) is tied to +5 V. Q rises at the first
  1MHzE edge after the write cycle, so IC34b sets one bus cycle after
  the write. Its Q_n output (pin 6) is ORed with S0_n by IC35 pins
  1/2 to 3, giving REQ_n. IC34b is cleared by PROC_n (pin 1).
- **When the write lands.**
  - IC27 (74LS374, clocked by phi1) registers REQ_n to PROC_n, so PROC_n
    is low during **slot 1** of the first channel after the request is
    posted.
  - During slot 1 the 74LS257 address multiplexers are disabled: their
    G_n is PROC. The latched host address (IC29) and the page register
    (IC31: A10-A8 and WRG_n) drive the internal address bus instead.
  - At the end of slot 1, IC26 and IC27 (both clocked by phi1) register
    that address and WRG_n. In **slot 2** the main RAM is therefore
    addressed with `(page bits 3..1) << 8 | offset`, and WR_n (IC27
    pin 19) is low if the page matched.
  - IC32 then drives the latched data onto the internal DATA bus, and
    IC28 (the main RAM) is written in the phi1-low half of slot 2. Its
    WE_n (pin 21) is IC30 pin 6, OR(IC2 pin 6, phi1), where IC2 pin 6 is
    AND(WR_n, CRST_n).
- **No cost to the DSP.** The address slot that the host steals (slot 1)
  is the one whose register row, `+&40`, is unused (section 2), so a
  host write never disturbs the DSP.
- **Timing.** A write lands at most about 2.4 us after the bus cycle:
  one 1MHzE period plus up to one 1.33 us channel. A request is posted
  for every JIM write whatever the page. If the page does not match,
  the transfer slot runs with WR_n high and nothing is written. If two
  JIM writes come before a transfer, the latches hold only the second,
  so the last write wins.

So a host write to JIM page P, offset X, with page nibble 3 stores the
byte at main RAM address `((P >> 1) & 7) << 8 | X`. This is exactly the
model every emulator uses.

### 1.3 Main RAM addressing (p1)

- **The RAM.** IC28 is an MB8128-10, a 2K x 8 static RAM with 100 ns
  access. CS_n (pin 18) is grounded. The scan's label looks like
  "IC25", but the clone BOM lists the two MB8128s as IC22 and IC28, and
  IC25 is the 74LS173A next to it.
- **Registered address.**
  - The low address byte comes from IC26, a 74LS374 with OE_n grounded,
    clocked by phi1. Its Q outputs reach RAM A7-A0 (RAM pins 1-8)
    through scrambled pin assignments that compose to a straight
    mapping: internal address bit n goes to RAM An.
  - The high bits come from IC27 (Q pins 16, 15, 12 to RAM pins 19, 22
    and 23, which are A10, A9 and A8).
- **The internal address bus** (bits 7-0, input to IC26) has three
  possible drivers:
  - IC23 and IC18, 74LS257 2:1 multiplexers. G_n is PROC and S is S6_n.
  - IC29, the host address latch (1.2).
  - IC25 (74LS173A, p1), which drives bit 7 and A10-A8 for the wave
    lookup (section 2).
- **Multiplexer wiring.** The 74LS257 output Y pins carry internal bits
  as follows: IC23 Y3, Y2, Y4, Y1 (pins 9, 7, 12, 4) give bits 7, 6, 5,
  4; IC18 Y3, Y4, Y1, Y2 (pins 9, 12, 4, 7) give bits 3, 2, 1, 0. With
  ADDR6..0 being the sequencer address (section 2), the inputs are:

| Bit | B input (S6_n high, every slot but 6) | A input (S6_n low, slot 6) |
|-----|---------------------------------------|----------------------------|
| 7 | INDEX (IC23 pin 10) | wave select bit 0 (IC25 Q4, pin 6, to IC23 pin 11) |
| 6 | ADDR0 (IC23 pin 6) | SUM7 (IC23 pin 5) |
| 5 | ADDR2 (IC23 pin 13) | SUM6 (IC23 pin 14) |
| 4 | ADDR1 (IC23 pin 3) | SUM5 (IC23 pin 2) |
| 3 | ADDR3 (IC18 pin 10) | SUM4 (IC18 pin 11) |
| 2 | ADDR6 (IC18 pin 13) | SUM3 (IC18 pin 14) |
| 1 | ADDR5 (IC18 pin 3) | SUM2 (IC18 pin 2) |
| 0 | ADDR4 (IC18 pin 6) | SUM1 (IC18 pin 5) |

- **High bits.** A10-A8 come from the pull-ups, so `111`, in every slot
  except slot 6 (driven by IC25, section 2) and the host slot (driven
  by IC31).
- **Register address.** The register read in slot s of channel-counter
  value k is at

      &700 | INDEX<<7 | s0<<6 | s2<<5 | s1<<4 | k0<<3 | k3<<2 | k2<<1 | k1

  where s2..s0 are the bits of s and k3..k0 the bits of k. The ADDR3-6
  bus taps are not individually labelled on p2. Their bit order is
  inferred from the counter stage order through IC13 (section 2); the
  FPGA's register address uses the same permutation.
- **Two consequences.**
  - **Rows.** Slot s reads register row (s0, s2, s1): slot 0 reads
    `+&00`, 1 reads `+&40`, 2 `+&10`, 3 `+&50`, 4 `+&20`, 5 `+&60`,
    (6 is the wave lookup), and 7 reads `+&70`.
  - **Columns.** Counter value k uses register column
    r(k) = (k0 << 3) | (k >> 1). The DSP visits the columns in the order
    0, 8, 1, 9, 2, 10, ..., 7, 15.
- **Main RAM OE_n** (pin 20) is IC5 pin 10, which inverts IC2 pin 6
  (AND(WR_n, CRST_n)). The RAM is read in every slot except during a
  host write or a reset.

### 1.4 What a read returns: the board is write-only

- **Nothing drives the bus.** The BBC data lines D0-D7 (connector pins
  18-25) go only to IC32's D inputs and to the IC31 decode gates (IC35,
  IC14). No device on the board has an output connected to them, and
  there is no transceiver. A read of `&FCFF` or of any JIM address gets
  nothing driven by the Music 5000; the CPU sees whatever the BBC's own
  1 MHz bus buffer reads from the floating lines.
  `docs/empirical-databus-behaviour.md` records `&FF` for FRED/JIM with
  nothing attached. This settles survey question 4 and confirms the ETI
  article.
- **But a read changes state.** Because the IC31 decode ignores R/W
  (1.1), a read of `&FCFF` loads the page register from the floating
  bus. If that reads as `&FF`, as with nothing else driving it, the
  upper nibble is F and the board is deselected until the next write to
  `&FCFF`. **OPEN (minor):** the value actually present during a read
  depends on the other devices on the bus.
  - The FPGA assumes otherwise: it loads its page register on writes
    only, returns the complement of the page register on reads of
    `&FCFF`, and returns RAM contents on JIM reads.
  - BeebEm returns the page register and RAM contents on reads.

  Neither is what this schematic does.
- **No read path to the RAM.** A JIM read has no effect on the RAM. A
  read is not decoded by IC35 pins 13/12 (they require R/W low), so it
  posts no request.

### 1.5 Reset (p1)

- **The reset signal.** RST_n from the bus connector (pin 14) passes
  through R1 (1K0) and C7 (22 nF) to give CRST_n, which has an RC
  constant of 22 us.
- **What CRST_n does while low:**
  - It clears the INDEX flip-flop IC3a (pin 1).
  - IC2 pin 6 goes low, so the main RAM's OE_n is high and its WE_n
    follows phi1. **The RAM is written in every slot.**
  - Nothing drives the internal DATA bus: IC32 is off because WR_n is
    high. RP1, eight 1K0 pull-downs, holds DATA at 0.
- **So reset writes zeros:**
  - into every register address the sequencer issues. With INDEX
    cleared, that is the normal bank `&700-&77F`: every channel's
    frequency, wave select, amplitude and control become 0.
  - and into the wave-table byte addressed in each slot 6. Once the
    wave selects are zero, that is wave 0 at index phase_hi >> 1 for
    each channel.
- **What reset does not touch:**
  - The alternate bank `&780-&7FF`.
  - The other wave bytes.
  - The phase RAM (IC22), whose write enable does not involve CRST_n.
  - The page register.
- **After reset** the frequencies are 0, so phases stop and no channel
  wraps. The amplitude store (section 4) therefore keeps whatever it
  held until a frequency is written and the phase wraps. B-Em's
  comment, "Real hardware clears 0x3E00 for 128 bytes and random
  waveform bytes depending on phaseRAM", agrees.
- **OPEN (minor):** whether BREAK on each host machine asserts RST_n on
  the 1 MHz bus connector, or only power-on reset does. This decides
  whether BREAK silences the synthesiser.

## 2. The sequencer (p2)

- **The clock.** A 12.000 MHz crystal oscillator (IC5 pins 1-2 and 5-6,
  R7, R8, C8, with C9 33 pF "not fitted as standard") drives IC9, a
  74LS393, at 1A (pin 1).
  - 1QA (pin 3), inverted by IC5 pins 13/12, is **phi1, 6 MHz**. Its
    rising edge clocks every register in the design.
- **The counters.** 1QB, 1QC and 1QD are the 3-bit slot counter. 1QD
  also clocks the second counter, 2A (pin 13), whose 2QA-2QD are the
  4-bit channel counter. Both CLR inputs go to ground through R6 (1K0),
  with an optional "*" link to +5 V that is not fitted, so the counters
  free-run.
- **One slot is one phi1 period.** 1QB toggles at every 1QA edge, so the
  slot advances once per phi1 period. A channel takes 8 slots (1.333 us),
  and a frame of 16 channels takes 128 slots (21.333 us), so the
  **sample rate is 46875 Hz**.

  The ETI article's "1.2us" and "19.2us" are wrong; its 47 kHz is right.
- **The registered sequencer address.** IC13 (74LS374, CK = phi1)
  registers the counters into ADDR6..ADDR0:

| Counter stage | IC13 D pin | IC13 Q pin | Signal |
|---------------|------------|------------|--------|
| 1QB | 14 | 15 | ADDR0 |
| 1QC | 3 | 2 | ADDR1 |
| 1QD | 8 | 9 | ADDR2 |
| 2QA | 13 | 12 | ADDR3 |
| 2QB | 7 | 6 | ADDR4 |
| 2QC | 17 | 16 | ADDR5 |
| 2QD | 4 | 5 | ADDR6 |

  IC13 D7 (pin 18) takes ADDR1 and gives **PA1** = ADDR1 delayed one
  slot (Q pin 19). IC27 does the same for ADDR2, giving **PA2** (p1,
  D pin 8 to Q pin 9).
- **Slot strobes.** IC10, a 74LS138, decodes ADDR2..0 into S0_n-S7_n
  (S2_n, S3_n and S5_n are unused). It is always enabled. ADDR0_n is
  IC5 pins 9/8.
- **Pipeline.**
  - A main RAM address formed during slot s is registered at the end of
    s (IC26, IC27).
  - The RAM data appears on DATA during slot s+1.
  - IC16 (74LS374, CK = phi1, p2) registers DATA at the end of s+1, so
    that byte is the adder's A input during slot s+2.
- **The adder.** IC6 (low nibble) and IC11 (high nibble), two 74LS283s,
  form an 8-bit adder: A from IC16, B from the phase path (3.2), sum on
  the SUM bus.
  - Carry-in (IC6 pin 7) is IC2 pins 12/13 to 11, AND(C4D, ADDR2).
  - Carry-out C4 is IC11 pin 9.
  - IC27 (p1) delays C4 twice: D pin 3 to Q pin 2 gives c4tmp, and D
    pin 4 to Q pin 5 gives C4D. So C4D in slot s is the carry out of
    slot s-2.

**The program.** k is the channel counter at the slot's start. "Row"
means the register row whose address was issued two slots earlier.

| Slot | Main RAM address issued | Adder A (row) | Adder B | SUM used for | Phase RAM (IC22) |
|------|------------------------|---------------|---------|--------------|------------------|
| 0 (of k+1) | `+&00` freq lo, ch k+1 | wave sample, ch k | stored amplitude, ch k | log product to DAC (4, 6); IC21 latches control of k | none |
| 1 | `+&40` (unused; host slot) | control, ch k | 0 | nothing | read byte 0 (phase lo) |
| 2 | `+&10` freq mid | freq lo | phase lo, or 0 if disabled | new phase lo | write byte 0 |
| 3 | `+&50` wave select | (`+&40`, unused) | 0 | nothing | read byte 1 |
| 4 | `+&20` freq hi | freq mid | phase mid, or 0 | new phase mid (carry in from slot 2) | write byte 1 |
| 5 | `+&60` amplitude | wave select | 0 | nothing; IC25 latched wave nibble at end of 4 | read byte 2 |
| 6 | wave table: `wave<<7` with SUM7..1 | freq hi | phase hi, or 0 | new phase hi (carry in from slot 4); its bits 7..1 index the wave | write byte 2 |
| 7 | `+&70` control | amplitude | 0 | amplitude, written to byte 3 on a wrap | write byte 3 if wrap, else read it |

- **Wave select.** IC25 (74LS173A on p1, CK = phi1, G1_n = S4_n, G2_n
  grounded, CLR grounded) latches DATA[7:4] at the end of slot 4. That
  is the wave-select register, issued in slot 3.
  - Its outputs are enabled only in slot 6: M = S6_n, N = PROC.
  - Q1-Q3 (pins 3, 4, 5) drive A10-A8, and Q4 (pin 6) drives
    internal bit 7.
  - So the wave address is `wavesel[7:4] << 7 | SUM[7:1]`, where SUM is
    the new phase-high byte computed in that same slot.
  - Only the upper nibble of the wave-select register is used. Wave
    numbers 14 and 15 address the register banks at `&700` and `&780`,
    which is why the article says "fourteen" tables.

## 3. The phase accumulator (p2)

### 3.1 Width and carry

- **Width.** Phase and frequency are 24 bits, added a byte at a time in
  slots 2, 4 and 6 through the 8-bit adder.
- **Carry between bytes.** Carry-in is AND(C4D, ADDR2): 0 in slot 2,
  and the carry out of slot 2 in slot 4, and of slot 4 in slot 6.
- **The phase RAM.** IC22 is an MB8128-10. Its address is:

| RAM pins | Address bits | Driven by |
|----------|--------------|-----------|
| 4, 5, 6, 7 | A4-A1 | ADDR6..3, the channel counter |
| 3 | A5 | PA1 |
| 22 | A9 | PA2 |
| 1, 2, 8 | A7, A6, A0 | grounded |
| 23, 19 | A8, A10 | +5 V |

  So each channel has 4 bytes, selected by (PA2, PA1) = ((s-1) >> 1)
  & 3: byte 0 phase lo, 1 phase mid, 2 phase hi, 3 stored amplitude.
- **When it is read and written.**
  - OE_n (pin 20) is IC14 pins 12/13 to 11, NAND(SX_n, ADDR0).
  - WE_n (pin 21) is IC4 pins 4/5 to 6, OR(IC14 pins 4/5 to 6, phi1),
    where IC14 pin 6 is NAND(IC14 pin 11, S0_n).
  - So the phase RAM is written, in the phi1-low half, in slots 2, 4
    and 6, and in slot 7 when SX_n is low. It is read in the odd slots
    when not writing. Slot 0 neither reads nor writes.
- **The phase data bus** is bidirectional.
  - IC17 (74LS244, G1_n and G2_n tied to the phase WE_n) drives SUM
    onto it while writing.
  - IC12 (74LS374, CK = phi1, OE_n grounded) registers the bus at every
    slot end, so it captures either the RAM output or the value being
    written.
- **The B input.** IC12's outputs reach the adder's B input through
  eight AND gates (IC8 and IC9, LS08). These are enabled by IC2 pins
  9/10 to 8, AND(ADDR0_n, LOAD_n) (3.2). So B is the latched phase byte
  in even slots and 0 in odd slots.
- **The wrap (C4D).** The carry out of the phase-high add in slot 6 is
  c4tmp in slot 7 and C4D in slot 0. IC14 pins 9/10 to 8 forms
  SX_n = NAND(c4tmp, S7). SX marks a phase wrap, the "c4d" of the
  emulators.
- **What uses the wrap:**
  - It writes the amplitude store (section 4).
  - It enables modulation (section 5), as C4D in slot 0.
  - It gates the slot-0 add through the carry-in: AND(C4D, ADDR2) is 0
    in slot 0 because ADDR2 = 0.

### 3.2 The disable / phase-set bit (freq lo bit 0)

- **The LOAD flip-flop.** IC3b (74LS74, D pin 12, CK pin 11, Q_n pin 8)
  takes D = DATA0. Its clock is an IC4 OR gate of S1_n and phi1 (the
  gate's pin numbers are illegible on the scan), so it latches at the
  end of slot 1. DATA in slot 1 is freq lo, issued
  in slot 0. The flip-flop is cleared asynchronously by S7_n (CLR,
  pin 13), so LOAD is valid in slots 2-6.
- **Effect.** LOAD_n gates the B input. With freq lo bit 0 set, B is 0
  in slots 2, 4 and 6, so each phase byte is written with
  `freq byte + 0`: **phase := frequency**, including bit 0 of the low
  byte, which is 1. Every carry is then 0, so the channel never wraps.
- **All eight bits.** IC16 registers all eight DATA bits, so bit 0 of
  freq lo is added in normal running too. It is 0 there, since setting
  it is what selects phase-set.
- **What this settles.**
  - B-Em, Pi1MHz and the FPGA load the phase with the frequency, which
    is right.
  - MAME and BeebEm zero the phase, which is wrong.
  - MAME's `& 0x7E` mask on freq lo is wrong.
- **A disabled channel still sounds.** It is not silenced: it plays a
  constant level, `wave[freq_hi >> 1]` scaled by the stored amplitude,
  every frame. Software silences a channel with its amplitude.

## 4. Amplitude (p1, p2)

- **The add.** In slot 0 the adder adds A = the wave sample of channel
  k (sign in bit 7, 1 = positive; log magnitude in bits 6-0) to
  B = channel k's stored amplitude, IC12's latch of phase byte 3. The
  carry-in is 0.
- **The gate.** IC15 pins 1/2 to 3 XORs SUM7 with SIGN (adder A bit 7);
  IC15 pins 4/5 to 6 inverts that, giving GATE_n = XNOR(SUM7, SIGN).
  The DAC registers load only when GATE_n is low, i.e. when the add
  flipped bit 7 (section 6). Writing m for the magnitude and a for the
  amplitude:
  - for either sign, the output log magnitude is `m + a - 128`, taken
    as SUM[6:0];
  - if `m + a < 128` the gate stays shut and the channel outputs zero;
  - with a >= `&80` the add can wrap past 255 and shut the gate at the
    loud end of the wave. Amplitude bit 7 is meant to be 0.

  This is the log-domain add with underflow clamp that B-Em, Pi1MHz,
  MAME and the FPGA model. BeebEm's linear multiply is wrong.
- **Latched at wave wrap.** The amplitude register (row `+&60`) is the
  adder's A input in slot 7, with B = 0, so SUM = amplitude.
  - If the phase wrapped in slot 6 (SX), the phase RAM writes SUM into
    byte 3, IC17 drives it onto the phase bus, and IC12 captures it.
  - Otherwise byte 3 is read back, and IC12 captures the old amplitude.

  Either way IC12 holds, during slot 0, the amplitude to use. **A newly
  written amplitude takes effect at the first phase wrap after the
  write, and applies to the very sample at which the wrap occurs.**
  B-Em, Pi1MHz and the FPGA model this; MAME and BeebEm apply it
  immediately.
- **Power-on.** Byte 3 is not cleared by reset, so a channel's
  amplitude after power-on is random until its first wrap.
- **The sign.** SIGN goes to the DAC as the sign bit after the
  inversion in section 6. It also goes to the modulation logic before
  inversion (section 5), so **INVERT does not affect modulation**.

## 5. Modulation and the alternate bank (p1)

- **IC21 as a register.** IC21 is a 74LS163A used as a 4-bit register:
  P and T are grounded, so it never counts. L_n (pin 9) is S0_n and CK
  (pin 2) is phi1, so it loads at the end of slot 0 of counter k+1. Its
  inputs are:
  - A (pin 3): IC30 pins 12/13 to 11, OR(C4D, SIGN). This is channel
    k's wrap or the positive sign of its sample.
  - B (pin 4): DATA7. Its output QB is unused.
  - C (pin 5): DATA5, the modulate bit of channel k's control register,
    which is issued in slot 7 and on DATA in slot 0.
  - D (pin 6): DATA4, the invert bit. QD (pin 11) is **INVERT**.
- **INDEX.** IC2 pins 1/2 to 3 forms AND(QC, QA), modulate AND (wrap OR
  positive). This is the D input (pin 2) of IC3a (74LS74). Its clock
  (pin 3) is IC4 pins 1/2 to 3, OR(S7_n, phi1), which rises at the end
  of slot 7. Q (pin 5) is **INDEX**, and CLR (pin 1) is CRST_n.
- **Where INDEX goes.** INDEX is internal address bit 7 in every slot
  except 6 (IC23 pin 10), so it selects the `&780` bank for all of a
  channel's register reads.
- **Timing.**
  - INDEX changes at the slot-7 to slot-0 boundary. IC26 samples the
    slot-7 address at that same edge, before INDEX's propagation delay
    takes effect, so the change applies from slot 0 of the following
    channel counter.
  - Channel k's result is loaded during counter k+1 and clocked at the
    end of counter k+1's slot 7, so **it selects the bank for counter
    k+2**. Each channel's eight register reads all come from one bank.
- **In register-column terms.** Counter value k uses column
  r(k) = (k0 << 3) | (k >> 1) (1.3), so k to k+2 means **column r
  modulates column r+1** for r = 0..6 and 8..14.
  - Column 7 (k = 14) modulates column 0 (k = 0) in the next frame.
  - Column 15 (k = 15) modulates column 8 (k = 1) in the next frame.

  The register layout is what makes "the next register column" equal
  "the channel processed two slots of eight later".
- **The emulators.** B-Em, Pi1MHz and MAME carry the flag from column i
  to column i+1 in register order, with 15 to 0 in the next frame. They
  agree with the hardware except at the two wraps: they pair 7 to 8 and
  15 to 0, where the hardware pairs 7 to 0 and 15 to 8.
  - The FPGA uses the same address permutation and slot timing as the
    TTL, so it should pair as the hardware does.
  - This is derived from timing, not read off a label. It is worth
    confirming against an FPGA simulation or a real-unit recording
    (OPEN, verification only).
- **When modulation fires.** The condition is evaluated per sample: the
  modulating channel's wave sample is positive (SIGN = 1), or its phase
  wrapped on this sample.
- **Reset.** INDEX is cleared by reset only. Otherwise it is rewritten
  at every channel boundary (D = 0 when not modulating).

## 6. Stereo: the time-slicing chopper (p2)

- **The DAC registers.** IC19 and IC20 are 74LS163As used as registers:
  P and T grounded, CK = phi1.
  - Sync CLR_n (pin 1) is S6_n, so they clear at the end of slot 6.
  - L_n (pin 9) is IC4 pins 9/10 to 8, OR(GATE_n, S0_n), so they load at
    the end of slot 0 only when the gate is open.
  - IC19 takes A-D = SUM6, SUM5, SUM4, SUM3. Its outputs QA-QD go to
    AM6070 B1-B4 (pins 3-6): the chord is B1-B3 = SUM6..4, and the MSB
    of the step is B4 = SUM3.
  - IC20 takes D, C, B = SUM2, SUM1, SUM0, with outputs to B5, B6, B7
    (pins 7, 8, 9). Its A input = SIGN, and QA (pin 14) goes through
    IC15 pins 9/10 to 8, XOR with INVERT, to SB (pin 2).
  - SUM7 never reaches the DAC.
- **The position counter.** IC24 is a 74LS163A, CK = phi1.
  - It shares L_n with the DAC registers (pin 9). Its inputs A-D (pins
    3-6) are DATA0-3: the control register's position nibble p, which is
    on DATA in slot 0.
  - Sync CLR_n (pin 1) is S6_n. P (pin 7) is +5 V; T (pin 10) is fed by
    its own QD (pin 11).
  - QD drives the AM6070's E/D pin (pin 1).
  - So the counter loads p at the end of slot 0, counts once per slot
    *only while QD = 1*, stops once it wraps past 15 (QD = 0), and is
    cleared at the end of slot 6.
- **What the DAC does in each slot.** The DAC registers hold the code
  for slots 1-6 and are zero in slots 7 and 0. During slots 1-6 the
  counter holds p, p+1, ..., p+5 (if counting), and QD selects the
  output pair:
  - E/D high sends current to IOE+/IOE-, which go to L+/L-;
  - E/D low sends it to IOD+/IOD-, which go to R+/R-.

  The E/D sense is from the DAC-76 datasheet as summarised in MAME's
  `dac76.h`: "E/D low: output on IOD".

| p | Left slots | Right slots |
|---|------------|-------------|
| 0-7 | 0 | 6 |
| 8, 9, 10 | 6 | 0 |
| 11 | 5 | 1 |
| 12 | 4 | 2 |
| 13 | 3 | 3 |
| 14 | 2 | 4 |
| 15 | 1 | 5 |

- **The weighted sum.** Seven positions, as the article says. Averaged
  by the output filter, the chopper equals a weighted sum with left
  weight `left_slots / 6`, the emulators' `PanArray` {0 x8, 6, 6, 6, 5,
  4, 3, 2, 1}.
- **Two second-order effects.** These show only above the audio band or
  as tiny delays, but are exact:
  - Each channel's current flows for 6 of the frame's 128 slots, at a
    fixed offset of k x 8 slots, i.e. k x 1.333 us into the frame. So
    the channels are time-staggered by up to 20 us.
  - The left and right shares of one channel are consecutive slots
    within that window, not simultaneous.

## 7. The DAC and the analogue path (p2, p3)

### 7.1 The AM6070 (IC36, p2)

- **Supplies.** V+ (pin 18) is +15 V; V- (pin 13) is -15 V; VLC
  (pin 10) is grounded.
- **Reference.** A 12 V zener ZD1 is fed from +15 V by R9 (1K0).
  - R10 (15K) runs from the zener to a node decoupled to ground by C12
    (22 nF). R11 (15K) runs from that node to VR+ (pin 11).
  - VR- (pin 12) goes to ground through R12 (15K).
  - With VR+ held near ground by the reference amplifier,
    Iref = 12 V / 30K = **0.40 mA**.
- **Code to current.** The DAC-76 / AM6070 has a full-scale output
  current of 3.8 x Iref, so **I_FS is about 1.52 mA**. (The datasheet is
  not in the repo; these figures are the datasheet's as quoted in MAME's
  `dac76.h`.) The output for chord C and step S is proportional to
  `2^C (2S + 33) - 33`, which runs from 0 to 8031 at full scale. All the
  implementations surveyed use this same curve.
- **Outputs.** IOE+ (pin 14) is L+, IOE- (pin 15) is L-, IOD+ (pin 16)
  is R+, and IOD- (pin 17) is R-. The sign bit selects which terminal of
  the pair carries the current.

### 7.2 The output stages (p3)

Left and right are identical. The table gives left / right
designators; all op-amps are TL072 (IC38 left, IC37 right) on +/-15 V.

| Function | Left | Right | Value |
|----------|------|-------|-------|
| I-to-V, minus terminal | R13, C13 | R25, C19 | 1K0 parallel with 22 nF, to ground |
| I-to-V, plus terminal | R15, C14 | R27, C20 | 1K0 parallel with 22 nF, to ground |
| Stage 1 input, minus | R14 | R26 | 15K, to inverting input |
| Stage 1 input, plus | R16 | R28 | 15K, to non-inverting input |
| Stage 1 feedback | R18, C16 | R30, C22 | 15K parallel with 3n3 |
| Stage 1 non-inverting leg | R17, C15 | R29, C21 | 15K parallel with 3n3, to ground |
| Stage 2 series resistors | R19, R20 | R31, R32 | 15K, 15K |
| Stage 2 capacitor, output to junction | C17 | C23 | 3n3 |
| Stage 2 capacitor, + input to ground | C18 | C24 | 3n3 |
| Stage 2 gain resistors | R22; R23 + R24 | R34; R35 + R36 | 15K; 15K + 15K |
| Output resistor | R21 | R33 | 1K0 |

- **The I-to-V conversion.** Each DAC output terminal drives a 1K0
  resistor with a 22 nF capacitor in parallel. The designer confirms
  these 1K resistors convert the current to a voltage
  (`stardot-key-points.md`, point 4).
- **Stage 1** is a unity-gain differential amplifier. Its feedback leg
  and its non-inverting leg are each 15K with 3n3 in parallel, giving a
  first-order pole at 1/(2 pi 15K 3n3) = 3215 Hz.
- **Stage 2** is a unity-R, unity-C Sallen-Key low-pass with gain
  K = 1 + 15K / 30K = 1.5: f0 = 3215 Hz, Q = 1/(3 - K) = 0.667.
- **The output.** Left goes to DIN pin 3 and right to pin 5, with
  ground on pin 2.
- **Order.** The chain from DAC current to output voltage is **fourth
  order**. The two 22 nF capacitors add a real pole at the I-to-V node:
  1K0 in parallel with the 15K input resistor (which sees a virtual
  earth) gives 937.5 ohm, and the pole is at 1/(2 pi 937.5 x 22 nF) =
  7717 Hz. The ETI article says "four-pole low-pass filter".
- **Overall response.** The response from either DAC terminal is the
  same. DC transimpedance is 1406 V/A; -3 dB is at 2115 Hz and -6 dB at
  2933 Hz (appendix).
- **Output levels.** At low frequency, one channel at full scale on one
  side contributes 1406 ohm x 1.52 mA x 6/128 = **0.10 V**. Sixteen
  channels in phase on one side give **1.60 V peak**. That is why the
  article calls the level "quite low in normal use". These use the
  quoted 3.8 x Iref and ideal op-amps.

## 8. Music 3000 and Music 500 variants

- **Music 3000.** The schematic shows no jumpers. The page match is
  hard-wired gates: OR(D7, D6) OR NAND(D5, D4), which match nibble 3
  only (1.1). A Music 3000 answers to nibble 5 (`0101`), which needs
  different gate inputs: the designer's "small track adjustment".
  **OPEN:** which tracks change. Every emulator, and the FPGA through
  its `id` generic, assumes the 3000 is identical apart from the nibble
  it matches.
- **Music 500 vs 5000.** This is Issue 1 (1984). The Music 500 to 5000
  change, needed for the BBC Master, is not shown. According to the
  Stardot clone thread it is a 470R in series with 1MHzE and 220pF to
  ground (`stardot-key-points.md`). That is consistent with this
  design capturing JIM writes on the *rising* edge of 1MHzE (1.2),
  where a little delay would give the write data time to settle.
  **OPEN (electrical only):** this does not change the emulated
  behaviour, since a bus write is atomic in the emulator.
- **The 74F04.** The gate legend specifies a 74F04 for the inverters
  (IC5), although the designer recalled no F parts (`stardot-key-points.md`,
  point 9). IC5 includes the 12 MHz oscillator inverters, which is a
  plausible reason for an F part.

## Appendix: the output filter's transfer function

- **Method.** Nodal analysis of the page 3 network with ideal op-amps.
  The DAC is a current source into one I-to-V node, with the other node
  undriven. The unknowns are the two I-to-V nodes, the stage-1
  non-inverting input (which is also the inverting input), the stage-1
  output, and the stage-2 junction and non-inverting input.
- **Poles** of the differential path:
  - a complex pair at 3215 Hz with Q 0.667 (stage 2), i.e. s = -15152
    +/- j13362 rad/s;
  - a real pole at 3215 Hz (stage 1, 15K with 3n3);
  - a real pole at 7717 Hz (the I-to-V node, 937.5 ohm with 22 nF).
- **Magnitude** relative to DC:

| Frequency | Gain |
|-----------|------|
| 1 kHz | -0.6 dB |
| 2 kHz | -2.7 dB |
| 3215 Hz | -7.2 dB |
| 5 kHz | -15.6 dB |
| 10 kHz | -34.4 dB |
| 20 kHz | -56.6 dB |
| 23.44 kHz (Nyquist) | -62.0 dB |

  The -1 / -3 / -6 / -20 / -40 dB points are at 1263 / 2115 / 2933 /
  5995 / 11979 Hz. The asymptote is -24 dB/octave, and there is no
  peaking.
- **Cross-check against the Stardot figures** (`stardot-key-points.md`):
  - **hoglet's "3214 Hz"** is f0 of stage 2 (and of stage 1's pole):
    3215 Hz here.
  - **Diminished's SPICE "red trace".** It drove the input pair with a
    voltage source, which shunts the 22 nF capacitors and leaves a
    third-order response. Modelled that way here, stage 1 times stage 2
    gives -3 dB at 2226 Hz, -6 dB at 3083 Hz and -20 dB at 6563 Hz,
    with a low-frequency gain of 1.5 (+3.5 dB). That matches the
    trace's shape and its "roughly 18 dB/octave". The real,
    current-driven chain adds the 7717 Hz pole.
  - **The designer's "not down 6 dB until nearly 6 kHz"** fits stage 1
    on its own (-6 dB at 5554 Hz), not the whole chain (-6 dB at
    2933 Hz). This may be a recollection of one stage. The component
    values are unambiguous on page 3.
  - **B-Em's and the FPGA's IIR** (a 3rd-order Butterworth with
    -3 dB at 3214 Hz) is noticeably wider than this network: its
    -3 dB point is about 1.1 kHz higher.
- **Reproducing the figures.** A short script solving the 6 x 6 nodal
  equations with numpy over a log-spaced frequency grid reproduces all
  of the above. It is not committed, since it is a calculation, not
  part of the emulator.

## OPEN items

1. **What a read of `&FCFF` loads into the page register.** The register
   loads on reads too, from the floating bus, which is probably `&FF`
   and so deselects the board. The value depends on what else is on the
   bus (1.4). The FPGA and BeebEm both differ from the schematic here.
2. **Whether BREAK asserts RST_n on the 1 MHz bus** on each host
   machine, and so whether BREAK clears the normal register bank (1.5).
3. **Music 3000 decode.** Which tracks change so that the board matches
   nibble 5 (8).
4. **Verify the modulation pairing at columns 7 and 15** (to 0 and 8)
   against an FPGA simulation or a recording (5). Derived, and believed
   right.
5. **AM6070 datasheet.** Confirm I_FS = 3.8 x Iref, the E/D sense and
   the code law from the datasheet itself, rather than MAME's quotation
   (6, 7.1). Only absolute level and the left/right assignment depend
   on it.
6. **Write capture on the rising edge of 1MHzE**, and the Music 500 to
   5000 E-delay mod (8). Electrical only; it does not affect the
   emulation.
7. **The ADDR3-6 tap order.** It is inferred from IC13's pin assignment,
   because the bus taps are unlabelled on p2 (1.3, 2). It is consistent
   with the FPGA and with the 4-bytes-per-channel phase RAM addressing.
