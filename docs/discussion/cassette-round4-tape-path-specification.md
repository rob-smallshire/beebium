# The tape path: cassette round 4 specification (#208)

Round 3 gave the ACIA and Serial ULA the clock model. This round adds the tape: the signal seam,
the UEF and CSW readers, the deck, and the cassette side of the ULA (data separator, high-tone
detector, tone synthesiser), reachable from the command line for tests. No service, no
indicator, no persistence and no sampled sources yet; those are rounds 5 to 7. Everything here
is from `cassette-specification.md` ("the programme") and the findings (F-numbers).

## `TapeSignal`

`src/core/include/beebium/cassette/TapeSignal.hpp`. The seam between a tape and the deck.

- Tape time is a `uint64_t` count of ULA clocks (16/13 MHz) from the start of the tape.
- A source yields items in tape-time order, each one of: `Edge{time}` (a transition of the
  squared signal), `Gap{until}` (no edges before `until`), `End`. A source never yields a time
  earlier than the last one it yielded.
- Interface: `next()` returns the next item; `seek(time)` repositions so the next item is the
  first at or after `time`; `rewind()` is `seek(0)`; `duration()` returns the total tape time
  when known, else nullopt; `kind()` returns `Uef`, `Csw` (later `Wav`, `Live`); `metadata()`
  returns the title, origin and target-machine strings the source carries, empty if none.
- All calls come from the emulation thread. A file source may read and decode ahead on that
  thread; it holds no lock.

## `UefTape`

`src/core/include/beebium/cassette/UefTape.hpp`, `.cpp` in `src/core/src/cassette/`. Reads a
file into memory at open (plain or gzipped: `1F 8B` magic, inflated with zlib, which the server
already links for CSW's Z-RLE and is in vcpkg), parses the chunk list once, and renders edges
chunk by chunk on demand.

Rendering rules (programme, "UEF"; F4.1):

- Header `UEF File!\0`, minor then major version byte; major must be 0, else refuse with "The
  file X is a UEF of version M.m, which Beebium does not read."
- Base frequency starts at 1200 Hz; carrier is 2 × base. A cycle is a low half then a high half
  of equal length, `clocks_per_cycle = round(1230769 / f)`; phase per &0115 (default 180°, which
  with the rendering above means a cycle begins with its high half; 0° begins low). An
  edge is emitted at every half-cycle boundary; the first edge after a gap is at the phase's
  starting level. Rendering as ideal squares is the programme's decision: UEF is phase
  insensitive.
- Bit encoding: 1200 mode, 0 = 1 cycle at base, 1 = 2 cycles at 2 × base; 300 mode (&0117 =
  300), 0 = 4 cycles, 1 = 8 cycles.
- &0100: each byte as start 0, 8 data bits LSB first, stop 1.
- &0104: three bytes then data: data bits per packet (7 or 8), parity 'N', 'E' or 'O', stop
  bits as a signed byte where a negative count −n means n stop bits followed by one extra short
  (2 × base) cycle. Frame: start, data LSB first, parity if any, stop bits. The MakeUEF
  parity swap: if the &0000 origin string names MakeUEF with a version below 2.4, 'E' and 'O'
  are exchanged (F3.4). Refuse other data-bit counts with a sentence.
- &0110: n cycles of carrier. &0111: n1 cycles, then &AA as 8N1 in the current encoding, then
  n2 cycles. &0112: a gap of n / (2 × base) seconds. &0116: a gap of f seconds (float32).
  &0113: base frequency becomes f Hz (float32); refuse f outside 100 to 10000 with a sentence.
  &0117: 300 or 1200, else refuse. &0115: phase 0 to 359 degrees, applied from the next cycle
  after a gap. &0114 security cycles: 24-bit count n, then byte 'P' or 'W' meaning the first
  cycle is a lone high pulse, byte 'P' or 'W' meaning the last is a lone low pulse, then
  ceil(n/8) bytes of cycles, least significant bit first, 1 = long (base) cycle, 0 = short
  (`Decision (unverified)`, the worked examples' order); a lone pulse is one half-cycle.
- Metadata kept: &0000 origin, &0001 instructions, &0005 target machine, &0009 short title,
  &0120 position markers, &0130, &0131. Ignored: &0003, &0006 to &0008, &000A, &0101, &0103,
  &0105. Refused with a sentence naming the chunk id: anything &0200 and above. Any other chunk:
  skipped, with one log line naming it.
- `duration()` is the rendered total; `seek` lands at a chunk boundary at or before the time
  and renders forward.

## `CswTape`

`src/core/include/beebium/cassette/CswTape.hpp`. F4.2:

- Signature `Compressed Square Wave\x1A`, major 1 (minor 1, u16 sample rate at &19, compression
  at &1B must be 1, flags at &1C, data from &20) or major 2 (u32 rate at &19, u32 pulse count,
  compression 1 RLE or 2 Z-RLE, flags, header-extension length HDR, 16-byte application name,
  HDR bytes, data). Refuse rate 0, unknown compression or any other version with a sentence.
- Each pulse byte is a half-cycle length in samples; &00 is followed by a u32 length. The
  initial level is flags bit 0 (set = starts high). Each pulse ends in an edge at
  `accumulated_samples × 1230769 / rate` ULA clocks, with the fraction carried. A pulse longer
  than 100 ms is a gap instead of an edge pair, so a detector that waits for silence sees
  silence.
- Z-RLE is inflated whole at open. `duration()` is the total.

## `CassetteDeck`

`src/core/include/beebium/cassette/CassetteDeck.hpp`, owned by each hardware policy beside the
disc drives, with `has_cassette = true` as a static trait on every policy and a `HasCassette`
concept.

State: `std::unique_ptr<TapeSignal> tape_` (null for none) with its name; `transport_`
(`Stopped`, `Playing`); `position_` in ULA clocks; the pending item from the source.

Interface: `insert(std::unique_ptr<TapeSignal>, name)` (throws `std::runtime_error` with a
sentence if occupied), `eject()`, `play()`, `stop()`, `seek(clocks)`, `position()`,
`duration()`, `transport()`, `has_tape()`, `name()`, `metadata()`.

Per ULA clock (called by the ULA's cassette side with the relay state): if `Playing` and the
relay is closed, advance `position_` by one clock and report to the caller whether an edge falls
on this clock (consuming it) and whether the tape is in a gap. On `End`, stop. A stopped deck,
or an open relay, reports no edges and time does not advance (programme, "The deck": the motor
stops the tape instantly). `seek` beyond the end stops at the end. Break and power cycle do not
touch the deck.

## `SerialUla`, cassette side

All in ULA clocks, from the programme's tables.

**Data separator.**

- An edge from the deck starts a burst: 4 RxC pulses of 4 clocks each (2 low, 2 high: 1.625 µs
  per half; 16 clocks a burst), with `receive_clock(rxd)` to the ACIA on each pulse's rising
  edge carrying the current RxD level. An edge during a burst
  ends the burst at once and starts a fresh one.
- A timeout of 350 clocks since the last edge (`Decision (unverified)`, 284 µs) with no new edge
  emits one more burst and marks the gap long. No further bursts until the next edge.
- RxD: at each edge, from the gap just ended: long gives 0; two consecutive short gaps give 1;
  otherwise RxD holds. In silence RxD holds.
- On the VLSI part with latch bit 3 set, RxD is inverted.

**High-tone detector and /DCD.**

- A cell counter advances once per 256 clocks while RxD is 1, high-tone edges are arriving (an
  edge within the last 350 clocks), and the motor bit is 1. It clears on RxD becoming 0 or the
  motor bit becoming 0. Silence ends in a timed-out gap, so the first edge after any silence
  recovers a 0 and clears the counter: silence re-arms the detector. (An earlier draft said the
  count held through silence; that cannot coexist with the separator rule, the replica clears
  on the recovered 0, and the Ferranti evidence ties the discharge to the timeout burst.)
- At the threshold (Ferranti 962 cells, VLSI 445) /DCD is driven high for one cell (256 clocks),
  then low, and the counter holds at the threshold until cleared.
- With RS423 selected /DCD is forced low but the counter runs as above. Switching to cassette
  does not clear it.
- The ACIA sees the pulse through `set_not_dcd` as today; round 3's latch does the rest.

**Tone synthesiser (transmit).**

- Runs whenever cassette is selected and RTS is low: a tone generator from the master clock,
  2400 Hz mark (512 clocks per cycle) for TxD 1 and 1200 Hz space (1024 clocks) for TxD 0,
  TxD sampled at 1024-clock cell boundaries at a zero crossing, phase-continuous. RTS high or
  RS423 selected: silence. On the VLSI part with bit 3 set the sense is inverted.
- Its output is a `TapeSignal`-shaped edge stream delivered to an optional `TapeSink`
  (`src/core/include/beebium/cassette/TapeSink.hpp`: `edge(time)`, `silence_until(time)`),
  which this round leaves unset: edges are dropped. TDRE follows the ACIA at the transmit
  divider as round 3 built it.

**Mode.** With cassette selected the ULA takes receive clocks only from the separator; the RS423
byte source's bytes are framed and lost as round 3 has it. /CTS low. Keep the RS423 framer
running in cassette mode: `HostSerialEndpoint` applies a pending re-point inside `has_data()`,
which only the framer polls, and machines boot with the latch at 0 (cassette), so stopping the
framer broke the host-serial re-point tests in round 3. That `has_data()`-as-tick contract is
implicit; add a comment at the poll site naming it, and nothing more this round.

## Command line and assembly

`--tape <filepath>` on every server: open the file, judge the format by content (UEF signature,
gzip magic then UEF signature, CSW signature), insert it and **play** (the CLI is for tests and
scripts; the service in round 5 inserts stopped). A bad file is a `CONFIG` (78) exit with the
sentence. `--tape` is a prelaunch option like `--disc`, registered where `--disc` is, and is not
yet a preset key.

## Tests

Test first. Catch2 names 7-bit ASCII, never starting with `--`.

### Tape builder, `tests/support/tape_builder.hpp`

A header-only writer for tests: `UefBuilder` (origin, carrier(cycles), gap(seconds),
bytes_8n1(span), block(name, load, exec, number, length, flags, data) which writes &2A, the
header, its CRC, the data and its CRC per F2.3, baud(300|1200) which emits &0117, raw chunk)
and `CswBuilder` (48 kHz, Z-RLE, from the same primitive calls, 10 and 20 sample half-cycles).
CRC-16/XMODEM with the check "123456789" = &31C3 as its own test. A standard tape is 5.1 s of
carrier, blocks with 0.9 s of carrier between, 5.1 s after (F2.4). A `short_leader` option
gives 0.62 s before and 0.25 s between (the Welcome tape's shape).

### `tests/test_tape_signal.cpp`, `[uef]`, `[csw]`

1. The CRC check value.
2. A `UefBuilder` tape round-trips through `UefTape`: the edges of one &0100 byte are at the
   expected clocks for 1200 mode and, after &0117 = 300, for 300 mode; a &0110 of n cycles
   yields 2n edges 256 or 257 clocks apart (tape time accumulates exactly at 16/13 MHz, so a
   2400 Hz half-cycle is 256.41 clocks); &0112 and &0116 yield gaps of the right length; &0113
   changes the spacing; &0115 changes the first level after a gap; &0111 contains &AA; &0104
   7E1 has a parity edge pattern and the MakeUEF swap applies only for an origin string below
   2.4; &0114 with the specification's worked example &9D &2C yields the cycle pattern
   LSLLLSSLSSLLSL.
3. Gzipped UEF reads the same as plain. A &0200 chunk is refused with the sentence; a &0400
   chunk likewise; an unknown chunk is skipped.
4. `seek` to the middle of a carrier lands within one cycle; `duration()` equals the last edge.
5. `CswTape`: a `CswBuilder` tape yields the same edge times as the `UefTape` of the same
   content within one ULA clock each (48 kHz has exact 10 and 20 sample half-cycles); v1.01 and
   v2 RLE headers read; polarity flag flips the first level; a 200 ms pulse becomes a gap; rate
   0 is refused.

### `tests/test_serial_ula_cassette.cpp`, `[serial_ula]`, `[cassette]`

6. Separator: a 2400 Hz edge train gives 4 RxC pulses per edge, 16 per 1200-baud bit, and RxD 1
   after the third edge; a 1200 Hz train gives a timeout burst after 350 clocks, 16 per bit, and
   RxD 0; a 90° intermediate gap of 384 clocks is short (no timeout); an edge 6 clocks into a
   burst truncates it to the two pulses issued so far.
7. A byte of 8N1 rendered as edges arrives in the ACIA at /16 intact; the same tape at 300
   (&0117) arrives at /64; at the wrong divider it does not.
8. Detector, Ferranti: 961 cells of carrier give no /DCD; 962 give a one-cell pulse and the ACIA
   latches status bit 2 and raises the IRQ with CR7 set; carrier continuing gives no second
   pulse; a 0 bit then 962 cells gives a second pulse; motor off then on then 962 cells gives a
   pulse; silence after 500 cells then carrier for 462 more gives no pulse, and 962 more does
   (silence re-arms); a
   write of the same latch value with the motor on does not clear the count; RS423 then cassette
   with the motor on does not clear it. VLSI: 445 cells.
9. Tone synthesiser: with cassette selected and RTS low, an idle ACIA produces edges 256 clocks
   apart; a transmitted &00 produces eight 1024-clock cycles between start and stop; RTS high
   produces no edges; with a `TapeSink` attached the edges arrive; TDRE returns after 10 bits at
   the divider with no sink.
10. Deck: `play` with the relay open advances nothing; relay closed advances; `stop` holds;
    `seek` and `rewind` reposition; `End` stops; a second `insert` throws the sentence; Break
    and power cycle leave position and transport alone.

### `tests/test_cassette_mos.cpp`, `[cassette][mos]`, Model B with MOS 1.20 as the test vector

11. `--tape` with a builder UEF holding a tokenised BASIC program (built at test time with
    `oaknut-basic tokenise` through the existing helper, or beebasm bytes with a `*RUN` path;
    skip with a sentence if the tool is absent): type `*TAPE` and `CHAIN""`, run in emulated
    time, read the screen text library for the program's output. At 1200 and, with `*TAPE3` and
    a 300 tape, at 300.
12. The CSW of the same tape gives the same screen.
13. A tape whose data CRC is corrupted gives `Data?` then `Rewind tape`; a tape whose second
    block is numbered 2 gives `Block?`.
14. The short-leader tape (0.62 s, 0.25 s) loads.
15. `*CAT` lists the file name and block numbers on the screen; the relay (latch bit 7) is
    on during the catalogue and the position advances only while it is on.
16. With the deck stopped the MOS sits at `Searching` and the relay stays on; `play` then loads.
17. Corpus cases, `SKIP` when `BEEBIUM_TAPE_CORPUS_DIRPATH` is unset or the file is absent,
    identified by SHA-256 from beebjit's list: Caveman Capers (DCD latch), StarDrifter (short
    carrier), Atic Atac (motor toggled during tone), Uridium CSW (tone-data-tone), Frak (master
    reset), Pro Boxing Simulator (transmit during load) each reach their first screen of play,
    judged by a screen-text string you record in the test after watching it once.

## Documentation

`docs/cassette.md` is round 5's; this round adds to `docs/serial-acia.md` only a pointer that the
cassette side of the ULA is documented there, and documents `--tape` in the server CLI help.

## Delivery

Commits: tape builder and `TapeSignal`/`UefTape`/`CswTape` with tests 1 to 5; the ULA cassette
side and the deck with tests 6 to 10; `--tape`, assembly and the MOS tests 11 to 17. Full gate
from a separate Release worktree before reporting done. The usual rules: `git commit --only`,
never amend, never push, no host or people names, no AI or model names in the body, your own
trailer. Stop and ask if a `Decision` proves wrong in the code, and tell me what MOS 1.20 does
if test 11 does not load first time: the separator and detector parameters are the suspects.
