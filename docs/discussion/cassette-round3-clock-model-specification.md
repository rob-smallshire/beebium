# The serial clock model: cassette round 3 specification (#208)

Refactoring in preparation for the cassette: the ACIA and Serial ULA move to the clock model of
`cassette-specification.md` ("the programme") before any tape exists, so that the tape rounds
add a signal source and nothing else. This round changes no behaviour an RS423 user sees at the
default ACIA divider; it changes what the model is made of. The gate is the existing serial
suite, green, plus the tests below.

## What changes

### `Mc6850`

1. **Clocked, not told a bit period.** The ACIA gains `receive_clock(bool rxd)` and
   `transmit_clock()`, each one external clock pulse, and divides by 1, 16 or 64 as its control
   register's counter-divide field says. `update_receive(bit)` and `update_transmit()` go.
   Batching forms (`receive_clocks(n, rxd)`) may exist for speed provided the sampling points
   are identical to n single calls.
2. **Receiver** (datasheet pp.4-532): at /16 and /64 it hunts for RxD low, confirms the start
   bit after 8 or 32 clocks of low, then samples each following bit 16 or 64 clocks later
   (mid-bit); a low shorter than the confirmation window is ignored. At /1 it samples RxD on
   every clock (external synchronisation). Framing, parity and overrun as today.
3. **Transmitter**: at each bit boundary (every 1, 16 or 64 clocks) it advances the frame and
   reports the new TxD level and bit kind (`Idle`, `Start`, `Data`, `Parity`, `Stop`) to the
   caller, as `update_transmit()` reports today, so the ULA can assemble bytes or drive tones.
4. **/DCD latch** (programme, "the high-tone detector"): a low-to-high transition on /DCD sets
   status bit 2 and raises the IRQ when CR7 is set; the bit stays set until the **data
   register** is read, however many times the status register is read in between; while /DCD is
   high the receiver is held in reset and RDRF reads clear. `Decision (unverified)`, from
   beebjit's and jsbeeb's measurement against the datasheet's status-then-data sequence.
5. **Master reset clears the control register** (`Decision (unverified)`, beebjit, Frak): after
   a write of &03 the control register reads as 0 through `control()`, so RTS is low and both
   interrupts are disabled until the guest writes it again.
6. The header comment citing b2's "REMOVED CLOCK DIVISION" is deleted; current b2 divides.

### `SerialUla`

1. **A ULA clock.** A fractional accumulator yields 16/13 ULA clocks per 2 MHz tick (as the
   Music 5000 resampler does); every interval below is in ULA clocks (812.5 ns). The baud table
   becomes a divider table: 19200 is 1 ULA clock per RxC/TxC, 9600 is 2, 4800 is 4, 2400 is 8,
   1200 is 16, 300 is 64, 150 is 128, 75 is 256 (so every rate is 0.16% above nominal, as on the
   machine).
2. **RS423 selected.** RxC and TxC are the receive and transmit divider outputs, delivered to
   the ACIA as clocks. The byte seam (`SerialDataSource`/`SerialDataSink`) is unchanged: a
   received byte is framed as today (start, 7 or 8 data bits, parity, stop bits as the ACIA's
   word select says) and **each level is held for 64 RxC clocks**, independent of the ACIA's
   divider, because the far end runs at the selected baud rate and does not know the ACIA's
   setting. At /64 the ACIA reads it as today. At /16 or /1 the guest reads garbage or framing
   errors, as on the machine. Transmitted bits are assembled into bytes from the ACIA's bit
   reports as today. RTS, BREAK and /CTS handling is unchanged.
3. **Cassette selected.** /DCD is **low** (no high-tone detector yet; it comes in round 4) and
   /CTS is low (AUG §20.7.4). RxD idles high with no RxC until round 4. TxC is the transmit
   divider output. Transmitted bits go nowhere: the RS423 sink receives nothing while cassette
   is selected.
4. **Reading &FE10** acts as a write of &FE (%11111110: motor on, **RS423** selected, receive
   75, transmit 2400; a stray read during a tape load therefore leaves cassette mode) and still
   returns the open-bus value (`Decision (unverified)`, every reference emulator and the
   replica).
5. **Part.** `SerialUla` takes a `Part { Ferranti, VLSI }` at construction, default Ferranti,
   stored and reported through `part()`. Nothing else uses it until round 4. No preset or CLI
   surface yet.
6. `tx_bit_period()` and `rx_bit_period()` accessors go; `tx_baud()`/`rx_baud()` stay and
   report the nominal table values. `SerialService` keeps reporting them; its
   `tx_bit_period`/`rx_bit_period` fields become the divider ratios and the proto comments say
   so (field numbers unchanged, so the fingerprint is unchanged).

### `SerialSocket`, hardware policies, `SerialService`

Only what the two changes above force. The socket still ticks the ULA twice per CPU cycle.

### Documentation

`docs/serial-acia.md`: the Timing section describes the clock model (ULA clock, dividers,
ACIA division, 64 clocks per RS423 bit, cassette mode's /DCD and /CTS), the &FE10 read rule,
the /DCD latch and master-reset rule marked as decisions, and the correction that MOS 1.20's
idle ACIA value is &56 (/64) and RS423 runs at /64; &15 appears only during cassette SAVE
set-up (findings §2.1, open question 26).

## Tests

### Catch2, `tests/test_mc6850.cpp`, tag `[mc6850]`

1. At /64, a byte presented as RxD levels held 64 clocks per bit is received intact for every
   word format; at /16, 16 clocks per bit; at /1, one clock per bit.
2. A byte presented at 16 clocks per bit to an ACIA at /64 is not received as that byte.
3. A low glitch on RxD of 7 clocks at /16, or 31 at /64, does not start a frame.
4. Transmit at /64: TDRE sets at the start of each character and a 10-bit 8N1 frame occupies
   640 clocks; 11 bits for 8N2 and 10 for 7E1; the reported TxD levels spell the byte LSB first.
5. /DCD: a rising edge sets status bit 2 and (CR7 set) the IRQ; three status reads later it is
   still set; a data read clears it; a second rising edge after it has fallen sets it again;
   with /DCD held high, RDRF reads clear although a byte arrived.
6. Master reset: after &03, `control()` is 0, RTS is low, and no IRQ is raised by a received
   byte until CR7 is written again.

### Catch2, `tests/test_serial_ula.cpp`, tag `[serial_ula]`

7. One emulated second of ticks yields 1 230 769 ULA clocks, give or take one.
8. For each of the eight rates, the RS423 receive and transmit clocks number 64 per bit and the
   byte rate is the table rate times 1.0016, within one part in ten thousand.
9. A byte from the source at every word format arrives in the ACIA at /64 (the existing
   loopback and format tests, re-expressed in clocks); at /16 the same byte is not received.
10. Reading &FE10 leaves `control()` at &FE, motor on, RS423 selected, receive 75 and transmit
    2400, and returns the
    open-bus value the socket supplies.
11. Cassette selected: /DCD and /CTS read low; a byte offered by the source is not received; a
    byte transmitted by the guest does not reach the sink; TDRE returns after 10 bits at the
    transmit divider.
12. Switching back to RS423 restores /DCD low, /CTS from the sink, and byte transport.
13. `part()` defaults to Ferranti and reports VLSI when constructed so.

### Regression

14. The whole `[serial]`-tagged suite, the host-serial, rpc-serial, loopback, ip232 and
    rfc2217 suites, the serial Python and TypeScript tests, and the gRPC serial status test, all
    green. Before changing anything, list any existing test that sets the ACIA to /16 or /1 on
    RS423 and expects bytes through; report it to the architect rather than changing its
    expectation.

## Delivery

One or two commits (`Mc6850` with its tests first if the split is clean, then `SerialUla` and
the socket with theirs), then the documentation. Full gate from a separate Release worktree
before reporting done. The usual rules: `git commit --only`, never amend, never push, 7-bit
ASCII test names not starting with `--`, no host or people names, no AI or model names in the
body, your own trailer. Stop and ask if a `Decision` proves wrong in the code or a test in 14
cannot be kept green without changing what it asserts.
