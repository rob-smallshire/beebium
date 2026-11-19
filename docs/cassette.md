# The cassette interface

The BBC Micro's cassette interface as Beebium builds it. It covers:

- the Serial ULA's cassette side: the data separator, the high-tone detector and the tone
  synthesiser, all clocked at 16/13 MHz;
- the MC6850 ACIA that divides the separator's clocks and latches /DCD;
- the tape sources: UEF and CSW images, WAV recordings through a model of the machine's
  analogue input, and a live audio input streamed by a client;
- the deck that plays them, and records on a UEF;
- the tape's signal map, where it carries signal a second at a time, and the append point it
  gives;
- the blank tapes the server ships;
- `CassetteService`, the motor LED, presets and machine directories, and `--tape`;
- where the GUI clients' deck and live input are specified, and what tests cover the whole.

The tracking issue is #208.

The programme specification is `docs/discussion/cassette-specification.md`. It holds every
decision and the reason for it, citing the round 1 survey in
`docs/discussion/cassette-research-findings.md` (cited below as "findings"). Each round has
its own specification beside it:

- `cassette-round3-*`: the clock model;
- `cassette-round4-*`: the tape path;
- `cassette-round5-service-specification.md`: the service, LED, persistence and clients;
- `cassette-round6-recording-specification.md`: recording and blank tapes;
- `cassette-round7-sampled-sources-specification.md`: WAV and the front end;
- `cassette-round8-live-audio-specification.md`: live audio input;
- `cassette-round9-signal-map-specification.md`: the signal map;
- `cassette-round10-append-point-specification.md`: the append point;
- `cassette-round11-live-input-gui-specification.md`: the live input in the GUI clients;
- `cassette-round12-live-input-meter-and-device-changes-specification.md`: the level meter and
  device changes.

The GUI clients' deck, including the live input's picker, level meter and messages, is
specified in chapter 9.0 of [client-specification.md](client-specification.md); routing another
program's audio into the live input is in [live-input-routing.md](live-input-routing.md). The
serial side of the same chips, RS423, is in [serial-acia.md](serial-acia.md).

## What is modelled and what is not

| Modelled | Not modelled (yet) |
|---|---|
| The ULA clocked at 16 MHz / 13 from a fractional accumulator on the 2 MHz tick, so every interval is a whole number of ULA clocks and the rates run 0.16% above nominal, as on the machine | The real part's 20 to 25 µs delay from an edge to its burst of receive clocks (not observable by software) |
| The data separator: a burst of four receive clocks at each edge, a 350-clock missing-edge timeout, the data rule from the gap just ended | Error correction: there is none on the machine either; a dropout reads as low tone |
| The ACIA's /DCD rising edge putting the receiver back to hunting, which drops a byte received but not yet read | |
| The high-tone detector: a counter of 256-clock cells, a one-cell /DCD pulse at 962 cells (Ferranti) or 445 (VLSI), and the rules that re-arm it | The Ferranti's edge-aligned pulse, its frequency acceptance threshold (about 1790 Hz), and the chip-to-chip spread of its 200 ms |
| The tone synthesiser: 2403.8 Hz for mark, 1201.9 Hz for space, phase-continuous, sampling TxD at each 1024-clock cell boundary | The staircase output levels and the VLSI part's opposite polarity |
| The ACIA dividing real receive and transmit clocks by 1, 16 or 64, and the /DCD latch | |
| The Ferranti part (the default) and the VLSI part, including the VLSI's tone inversion on latch bit 3 | Selecting the VLSI part: every machine is assembled with the Ferranti, and no preset key chooses |
| UEF (plain or gzipped) and CSW (v1 and v2, RLE and Z-RLE) tapes, rendered as edges in ULA clocks | |
| WAV recordings, squared by a model of the LM324 input stages: the DC block, the Sallen-Key high-pass and low-pass, and a comparator with hysteresis | The clipper's gain and the ULA input's threshold as such; board-to-board variation |
| One deck per machine: insert, eject, play, stop, rewind, fast forward, seek; the tape moving only while it plays with the relay closed, or while it winds | Motor inertia: the relay starts and stops the tape instantly. Tape audio: the tape reaches only the Serial ULA, and no client plays it |
| The motor relay as an indicator | |
| Recording on a UEF, written back to its file; four blank tapes | Recording on a CSW or a WAV |
| A live audio input streamed by a client, through a primed ring and the same front end; in the GUI clients, a picker of the host's inputs, a level meter and the messages of rounds 11 and 12 | Resampling or clock recovery: a live load is for real speed |

Nothing in the model knows what any MOS does with the cassette. MOS 1.20 appears only in
tests, as a test vector.

## The hardware

The cassette shares the 6850 ACIA (&FE08-&FE09) and the Serial ULA (&FE10, a write-only
latch) with RS423. Latch bit 6 selects between them, 1 for RS423 and 0 for cassette. Bit 7
drives the relay that switches the recorder's motor through the REMOTE lead, and it lights
the cassette motor LED on the keyboard. [serial-acia.md](serial-acia.md) gives the register
maps, the latch's rate fields, and the read of &FE10 that acts as a write of &FE.

With cassette selected, the ULA's cassette side provides three of the ACIA's inputs: the
receive clock, RxD and /DCD. /CTS is held low. The ACIA's TxD drives the tone synthesiser,
which is all the ACIA's transmit side reaches; the RS423 sink receives nothing.

`SerialUla` (`serial/SerialUla.hpp`) is the ULA. `Mc6850` (`devices/Mc6850.hpp`) is the ACIA.
`SerialSocket` (`serial/SerialSocket.hpp`) assembles the two for every machine.

## The clock model

The ULA runs from 16 MHz / 13 = 1.2308 MHz, and one ULA clock is 812.5 ns. Beebium ticks the
serial socket at 2 MHz. The ULA keeps a fractional accumulator of ULA clocks per tick, so every
interval in the tables below is an exact number of ULA clocks. Tests assert in ULA clocks.

The ACIA is clocked; it is not told a bit period. With cassette selected the transmit clock
is the ULA's transmit divider output. The MOS selects 300 baud, which is 19.23 kHz, and the
ACIA's /16 or /64 then gives 1200 or 300 baud. The receive clock comes from the data
separator, and the latch's receive rate field is ignored. The cassette's speed is therefore
chosen by the ACIA's divider alone:

- MOS 1.20 reads at `&D5` (/16, 1200 baud);
- it reads at `&D6` (/64, 300 baud) after `*TAPE3`.

### The data separator

| Parameter | Value |
|---|---|
| Missing-edge timeout | 350 clocks (284 µs) |
| Burst | four receive clocks of four ULA clocks each (`BURST_PULSES`, `PULSE_CLOCKS`), 16 clocks in all, starting at the edge |
| Edge during a burst | ends it; the new edge starts a fresh one |
| Data rule | at each edge, from the gap just ended: a long gap (one that timed out) gives 0, two consecutive short gaps give 1, otherwise RxD holds |
| Polarity | edges of either sense are equal |

A 2400 Hz half-cycle (256 clocks) never times out, and a 1200 Hz half-cycle (512 clocks) times
out once, emitting a second burst. So a 1200-baud bit of either tone gives 16 receive clocks
and a 300-baud bit gives 64. Silence gives one timeout burst after the last edge and then
nothing, and RxD holds its value.

### The high-tone detector and /DCD

A counter advances once per 256-clock cell (208 µs) while all three of these hold:

- RxD is 1;
- edges are arriving;
- the motor bit is set.

At the threshold the ULA drives /DCD high for one cell, and the counter then stops until it
is re-armed:

- Ferranti: 962 cells, 200 ms;
- VLSI: 445 cells, 92.7 ms.

Re-arming clears the count and drives /DCD low. These re-arm it:

- a recovered 0;
- the motor bit going to 0, which is what the MOS's 1 ms motor blip before each block is for;
- silence. While no edges arrive the count waits. The first edge after the silence ends a
  long gap, so RxD becomes 0, which clears the count.

Latch writes that keep the motor on do not re-arm it, nor does switching from RS423 to
cassette. With RS423 selected /DCD is forced low but the counter still runs.

On the ACIA side, a rising edge of /DCD sets status bit 2 and raises the IRQ if CR7 is set.
The bit stays set until the data register is read. The same edge clears RDRF and puts the
receiver back to hunting for a start bit, so a byte received but not yet read is lost. While
/DCD is high the receiver is held idle and RDRF reads clear.

### The tone synthesiser

With cassette selected and RTS low, the ULA samples TxD at each 1024-clock cell boundary
(832 µs). It fills the cell with two cycles of 2403.8 Hz for a 1 (mark) or one cycle of
1201.9 Hz for a 0 (space), phase-continuous, as edges to an attached `TapeSink`, or to the deck
while it records; a sink attached takes them before a recording deck does. RTS high or RS423
selected gives silence. A master reset of the ACIA does not: it clears the control register, so
RTS is low, and holds TxD at mark, so the synthesiser sends carrier. With neither sink nor
recording the edges are discarded. Bytes still clock out at the real rate, and TDRE returns
when the ACIA says so, which some loaders observe.

A byte written to the ACIA's transmit data register while the transmitter is idle is taken one
full bit time after the write: the write restarts the bit-rate counter. The datasheet says only
"within 1-bit time"; the measurement that would settle it is issue #291, and Decision 12 below.

### The part

`SerialUla` takes a part, Ferranti or VLSI, at construction. `SerialSocket` constructs it with
the default, so every machine Beebium assembles has the Ferranti part, as every Model B family
machine did; nothing in a preset chooses the VLSI, which only tests select. The two parts differ
in:

- the detector threshold;
- the tone inversion on latch bit 3, which the VLSI applies on receive and transmit and the
  Ferranti ignores;
- transmit polarity, which is not yet modelled.

`DeckStatus.ula_part` reports the part.

## Tape sources

`TapeSignal` (`cassette/TapeSignal.hpp`) is the seam between a tape and the deck. A source
yields, in tape time, a sequence of three kinds of item:

- an edge;
- a gap (no edges until a given time);
- the end.

Tape time is counted in ULA clocks from the start of the tape. A source can be rewound, seeked
and asked its duration. Once a source is the deck's tape, only the emulation thread calls it;
opening and parsing happen before that, on the thread that inserts (a gRPC thread for
`InsertTape`), and the signal map's worker walks a separate source of its own, never the deck's.

`TapeKind` is `Uef`, `Csw`, `Wav` or `Live`, reported on the wire as the `format` strings
`UEF`, `CSW`, `WAV` and `LIVE`.

`open_tape_file` (`cassette/TapeImage.hpp`) opens a file and judges it by content, not by
extension. Its refusals are sentences naming the file, each with a `TapeImageError` kind:

- `CannotOpen`: the file could not be found or could not be opened;
- `Unreadable`: the file is empty or could not be read, its gzip is damaged, or a chunk or
  header is truncated;
- `UnsupportedFormat`: it is not a UEF, CSW or WAV tape image (a gzip holding something other
  than a UEF, or a WAV of a bit depth or encoding Beebium does not play, is this too);
- `InvalidImage`: it is a UEF, CSW or WAV whose contents break the format: a UEF major version
  other than 0, an unknown &0104 parity, a CSW version or compression it does not know, or a
  malformed WAV.

### UEF

`UefTape` reads the `UEF File!` signature with major version 0 and any minor version, plain
or gzipped (it recognises the `1F 8B` magic). It renders each chunk as ideal square cycles at
the current base frequency. The default base is 1200 Hz, with carrier at twice it. The phase
chunk &0115 sets which half the first cycle after a gap starts with; the default is 180°, so
that cycle starts with its high half. Because the cycles are ideal, UEF loading does not
depend on phase.

| Chunks | Treatment |
|---|---|
| &0100 | Bytes, 8N1 |
| &0102 | Explicit 1200-baud bits: the first byte the unused bits at the end of the last, then the bits least significant first, a 0 one cycle at the base, a 1 two at twice it |
| &0104 | Bytes in the chunk's own format: parity, and negative stop-bit counts. The parity swap of MakeUEF before 2.4 is applied when the &0000 origin string names it. Other than 7 or 8 data bits, or an unknown parity letter, is refused |
| &0110 | Carrier |
| &0111 | Carrier, the dummy byte &AA, carrier |
| &0112 | A gap of n / (2 × base) seconds |
| &0113 | A change of base frequency, in the canonical meaning; outside 100 to 10000 Hz is refused |
| &0114 | Security cycles, least significant bit first, 1 a long cycle; the P flags that make the first or last a lone half-cycle are honoured |
| &0115 | Phase |
| &0116 | A gap in seconds, as a float |
| &0117 | The bit encoding: 1200 (one or two cycles a bit) or 300 (four or eight); other values are refused |
| &0000, &0001, &0005, &0009, &0120, &0130, &0131 | Kept as metadata: origin, instructions, title, target machine and position markers; origin, title and target machine reach the wire, the rest is written back unchanged |
| &0003, &0006 to &0008, &000A, &0101, &0103, &0105 | Ignored |
| &0200 and above | The file is refused with a sentence naming the chunk: it holds a disc, ROM or machine state, not a tape |
| Anything else | Logged and skipped |

The writer (below, under Recording) emits a subset of this table, so whatever it writes reads
back.

### CSW

`CswTape` reads any v1.x (RLE) and any v2.x (RLE or Z-RLE), at any sample rate. It honours the
initial polarity flag and the &00 long-pulse escape. Each pulse's edge falls at the running
total of samples × 16 MHz / (13 × rate), in whole ULA clocks, so no error accumulates. A pulse
longer than 100 ms is treated as a gap. A sample rate of 0 is refused in either version. A v2
header's application field becomes the tape's origin.

### Sampled sources: WAV

A `SampledSource` (`cassette/SampledSource.hpp`) is a tape as sound: mono samples in [-1, 1) at
a rate, pulled in blocks. `WavSource` reads RIFF/WAVE files of 8-bit (unsigned), 16, 24 and
32-bit integer and 32-bit float samples, at any rate, mono or stereo, plain or
`WAVE_FORMAT_EXTENSIBLE`. It reads the file on demand through a block cache, so a WAV of any
length plays without being loaded whole. A stereo file plays the channel the client asks for
(`InsertTape`'s `channel`, `"left"` or `"right"`), or else the one with the higher RMS over the
first 10 seconds, the left when they are equal; a channel given for a mono file is ignored.
ADPCM, mu-law and other encodings, more than two channels, and a data chunk that runs past the
end of the file are refused with a sentence naming the file.

`SampledTape` (`cassette/SampledTape.hpp`) is the `TapeSignal` over a source: it pulls samples
through `FrontEnd` (`cassette/FrontEnd.hpp`) until the model flips, and reports each flip as an
edge. A sample's tape time is its index × (16 MHz / 13) / rate, and an edge falls between the two
samples that straddle the threshold, by linear interpolation, so edge times are not quantised to
the sample period. A seek starts the model afresh, at rest. A WAV cannot be recorded on.
`TapeMetadata` reports its `sample_rate` and, for a stereo file, the `channel` played; a mono file reports none.

`FrontEnd` models the machine's LM324 input stages (findings 1.5 and 7.1), at the source's rate:

| Stage | Model | Constant |
|---|---|---|
| DC blocking | One-pole high-pass | 20 Hz |
| Sallen-Key high-pass | Second-order, bilinear transform | 1.29 kHz, Q = 1 / (3 - K), K = 1.27 (Q 0.578) |
| Sallen-Key low-pass | Second-order, bilinear transform | 4.13 kHz, Q = 1 / (3 - K), K = 1.26 (Q 0.575) |
| Comparator | Hysteresis on the filtered signal; no normalisation, no automatic gain | ±0.02 of full scale |

The Q is the circuit's own, from its equal components and stage gain. The filters run at an
internal rate that is the smallest whole multiple of the source's rate at or above 88.2 kHz
(96 kHz for a 48 kHz source), with the input interpolated linearly between samples (a
symmetric kernel, so no phase): designed at 22.05 kHz, the bilinear transform's warping moved the
1200 Hz tone by 7 clocks. The comparator reports at most one edge per source sample. At 48 kHz the low tone's crossings move by +67° and the high tone's by
-3°, as the circuit's simulation has it (+65° and -5°). The filters pass 0.51 of the low tone
and 0.71 of the high, and about -14 dB at 8 kHz; 50 Hz hum at full scale gives no edges. The
model costs 14.6 ns a sample at 48 kHz on an Apple Silicon Mac, and a 48 kHz WAV playing with
the motor on adds about 0.1% of real time to the emulation thread's work.

The phase a recording arrives in matters. Through this model a tape recorded as sines at 0°
does not load, because the low tone's +65° shift against the high tone's -5° moves the
half-cycles at each change of tone across the Serial ULA's 350-clock timeout; the same tape at
90°, the phase a playback head's differentiation gives a recording, loads, as do square waves.
If a WAV made by a tool rather than recorded from a deck will not load, its phase is the first
thing to check.

### Live input

To feed the live input from another program's audio rather than a real player, see
[live-input-routing.md](live-input-routing.md). What the GUI clients make of it, the picker,
the level meter and the messages, is in client specification 9.0.

A client can stream an audio input on its host, a real cassette deck's line-out, to the deck
as the live tape, through `CassetteService.StreamAudio` (its contract is in the RPC table
below). The samples go through the same front end as a WAV's. A stereo stream plays the channel
the client names, or the left; the server does not pick the louder, since it has not heard it.

`LiveSource` (`cassette/LiveSource.hpp`) is a `SampledSource` fed through a lock-free ring of two
seconds at the stream's rate: the thread the stream arrives on decodes the samples (S16 or F32,
little-endian, one channel taken from two) and pushes them; the emulation thread takes one
sample a sample period of emulated time. The ring is the only seam between them
(`docs/emulation-thread-ownership.md`). Neither end waits:

- **Priming.** The consumer starts by taking silence, uncounted, until the ring holds a quarter of
  a second, a cushion against the jitter of the stream's arrival. It adds a quarter of a second
  of latency between the deck and the machine.
- **Underrun.** When the ring runs empty the consumer takes silence and counts each sample it
  lacked until the ring has primed again. The machine hears a dropout.
- **Overrun.** When the ring is full the producer drops the oldest samples to make room and counts
  them, so the newest are kept. The machine hears a discontinuity.

There is no resampling and no clock recovery: a live load is for real speed. At real speed the
host's clock and emulated time drift by parts per million and the ring absorbs it for hours;
below real speed the ring overruns at the rate of the shortfall, above it underruns.
`DeckStatus` carries `live_ring_fill` (seconds of audio the ring holds), `live_overruns`
(samples dropped) and `live_underruns` (samples made up as silence) while a live tape is in,
and the GUI clients turn three consecutive seconds of growth in either count into one message
(client specification 9.0).

`LiveTape` is the `TapeSignal` over the source and a `FrontEnd`, of format `LIVE`. It is paced by
emulated time: each item it gives is one sample's edge or a gap to its time. It has no end and
no length. The deck plays it whenever it is playing, whatever the relay: a deck without a remote
lead keeps playing and the real ULA keeps listening, and the relay still re-arms the high-tone
detector. Stopped, the deck takes the samples and they go nowhere, so the ring does not fill.
Seek, rewind, fast forward and record are refused with `LIVE_SOURCE` ("The live input X
cannot be recorded on.", and likewise seeked, rewound or fast-forwarded). The position is tape
time as for any tape, advancing on every ULA clock from insertion whatever the transport, which
is the samples taken, priming silence included.

The live tape goes in, stopped, when a stream's format arrives, named after the device the
client gives, or "Live input" when it gives none. When the client closes the stream, a playing
deck plays out what the ring holds and the tape goes when it is empty, as a tape that runs out;
a stopped deck lets it go at once. `EjectTape` ejects it at once and ends the stream. The
stream's summary gives the samples received, dropped and made up as silence.

On this Mac a 48 kHz stereo WAV streamed live with the deck playing adds 0.35% of real time to
the emulation thread's work: the ring, the front end, and a tape item a sample.

## The deck

`CassetteDeck` (`cassette/CassetteDeck.hpp`) belongs to the hardware policy, beside the disc
drives. Every machine Beebium builds has one. Presence is a policy trait, `has_cassette`, so a
machine with an ACIA but no cassette interface can say no.

The deck holds:

- the tape: none, or one source with its name, URL, kind (UEF, CSW, WAV or live) and metadata;
- the transport: stopped, playing, recording, rewinding or fast-forwarding;
- the position, in tape time;
- the relay, from the ULA.

The deck's rules:

- **Movement.** Tape time advances while the transport is playing and the relay is closed. The
  relay stops the tape instantly, so the MOS's 1 ms blip stops it for 1 ms.
- **Insert** leaves the deck stopped at the start. A second tape is refused while one is in:
  removing a tape is a deliberate eject.
- **Play and stop** are the user's. A client may offer insert-and-play as one gesture, made as
  two requests.
- **Rewind and fast forward** wind the tape backwards or forwards at 30 times real speed
  (`WIND_SPEED` tape clocks per ULA clock), whether or not the relay is closed, and no edge
  reaches the ULA: a winding tape is off the head. A wind ends on stop, play, seek or eject,
  or at an end of the tape, where the deck stops. Rewind at the start, or fast forward at the
  end, stops at once. Play during a wind plays from where the tape has reached.
- **Seek** moves to a tape time at once; a seek to zero is the instant return to the start.
  A seek ends a wind and leaves a playing deck playing. A seek beyond the end lands at the end
  and stops the deck. At the end of the tape the transport stops; the tape does not loop.
- **Record** is a transport like play; see Recording below. Record at the end of the tape
  succeeds and leaves the deck stopped; record while recording does nothing.
- **Eject** stops the deck and removes the tape.
- **Reset.** Break and power cycle leave the deck alone. The relay opens because the latch
  resets.

The deck publishes its state through atomics: presence, transport, relay, position and a tape
generation. While the deck plays, records or winds, the position is published every 4096 ULA
clocks (`POSITION_PUBLISH_CLOCKS`, 3.3 ms), so the published position lags the head by at most
that. The service reads these without stopping the machine.

## Recording

A UEF can be recorded on; a CSW or a WAV is only read, and `record()` on one is refused with a
sentence naming the tape and its format ("The tape X is a CSW image, and Beebium records only
on UEF tapes."). A UEF that carries an &0113 change of base frequency is read only too, for
now: the writer describes every tape at the nominal 1200 Hz base and would lose the changes, so
`record()` is refused with `READ_ONLY` ("The tape X changes its base frequency (UEF chunk
&0113), which Beebium cannot yet write back, so it does not record on it.").

A UEF is recordable only where its recording can be kept: in a file the server can write, in
a folder it can write, since a write-back is a new file renamed over the old. A UEF on a
read-only file or in a read-only folder is refused ("Beebium cannot write the tape ro.uef or the
folder it is in, so it does not record on it; record on a copy you can write."), and so is one
held only in memory with no file, which only tests insert ("...is held only in memory, with no
file to keep a recording in, so Beebium does not record on it."). The deck asks the file system when
a key is pressed and when the status describes the tape, never on the clock path. Every
refusal's sentence is also `TapeMetadata.record_refusal`, so a client can show why before the
user presses record.

Recording is a transport, as on a real deck: the user presses
record, and from then on, while the relay is closed, the tape moves as it does in play and
every clock that passes under the head replaces what the tape held there with what the Serial
ULA's tone synthesiser sends, an edge or nothing. RTS high, or RS423 selected, sends nothing, so
a recording deck erases to silence while the Beeb is quiet, as a real one does. A relay opening
only stops the tape. Stop, play, a seek, a wind, eject and the end of the tape end a recording.
While it records the deck is the synthesiser's sink, and no edge reaches the data separator.

The first recording turns the tape into an `EdgeTrack` (`cassette/EdgeTrack.hpp`): every edge
held exactly in tape time, in blocks of alternating levels with 16-bit differences, two bytes an
edge, so a fully recorded C90 is about 52 MB. Recording splits the track at the head, drops the
old edges that pass under it, appends the new ones, and joins what is left when it ends.

A tape recorded on is written back to its file when a recording ends, on eject, on Save and on
a move of its machine, at shutdown, and when the deck is destroyed, and only if it has been
recorded on since it was last written. The whole file is written beside the old one as
`<file>.beebium-writing` and renamed over it, gzipped if it was gzipped. A file that cannot be
written is left whole, the failure is logged in a sentence, and the recording stays in memory
to be written at the next of those moments.

Beebium rewrites a recorded-on UEF in its own chunk vocabulary at the nominal 1200 Hz base: its
content is preserved cycle for cycle; its chunk layout and absolute timing (nominal, as in any
UEF) are not. Read back, every half-cycle is in the same class (2400 or 1200 Hz) and every gap
is within one 1200 Hz half-cycle of its length. The machine's tones are 0.16% faster than the
nominal base, so a recording's edges come back stretched by 1200 / 1201.92, as a real machine's
SAVE described by any UEF tool is. The metadata chunks (those below &0100, and &0120, &0130,
&0131) are written first, unchanged.

`classify_tape_edges` (`cassette/UefWriter.hpp`) walks the edges in regions separated by more
than two 1200 Hz half-cycles (1024 clocks) of silence. It classes each half-cycle as short or
long by the nearer nominal length, pairs them into cycles, and describes each region as:

| What it sees | Chunk |
|---|---|
| Silence | `&0112` when it is a whole number of 1200 Hz half-cycles to within two clocks and that number fits 16 bits; otherwise `&0116`, seconds |
| A run of 24 or more short cycles (`CARRIER_RUN_CYCLES`) | `&0110` carrier, at most 65535 cycles a chunk; a shorter run among bits that do not frame is written as 1 bits |
| Bytes framing cleanly as 8N1, the start bit after mark or at the region's start, at 1200 baud (one long or two short cycles a bit) or 300 (four long or eight short) | `&0100`, preceded by `&0117` when the baud differs from the one in force (1200 at the start of the file) |
| Bytes framing cleanly as 8E1, 8O1, 7E1 or 7O1 instead | `&0104` in that format |
| Anything that does not frame | `&0102`, explicit 1200-baud bits |
| A half-cycle without a partner of its class, at a recording's edge | Absorbed into the silence beside it |

A run of bytes keeps the format and baud it started in while they frame. Where a run starts,
the classifier takes the format and baud whose run of frames covers the most cycles, the baud
in force and the order of the table breaking ties: a lone 300-baud byte also frames at 1200
baud, as a start bit and three 0s, but its run is shorter. A 7E1 or 7O1 frame is as long as an
8N1 frame, with its parity bit where bit 7 is, so it is written as the 8N1 byte it is cycle for
cycle. The last chunk is the silence that keeps the tape its length.

MOS 1.20's SAVE is the test vector: a leader of about 5.1 s of carrier with one dummy &AA byte
in it (the &0111 pattern), each block as `&0100` bytes beginning &2A and the file name, 0.9 s of
carrier between blocks, and about 5.1 s of carrier after the last, with `&0117` 300 before the
data after `*TAPE3`. Its tail of carrier depends on the ACIA holding TxD at mark through the
master reset the MOS issues with a character just begun (see
[serial-acia.md](serial-acia.md)).
That last character can also leave a trace: when its start bit reaches TxD before the master
reset and a synthesiser cell samples it, the recording ends with a stray &FF frame after the
last block's CRC, as a real machine's can. Loading never reads past the CRC, so it is harmless.

## Blank tapes

The server ships four blank tapes in `blank-tapes` beside the blank discs (see
[deployment.md](deployment.md)): `C15.uef`, `C30.uef`, `C60.uef` and `C90.uef`, labelled by
the C-number that gives their minutes. Each is a UEF 0.10 holding an &0000 origin chunk reading `Beebium` and one &0116 gap of its length (900, 1800, 3600 or
5400 s, exact in float32), with no date and no other chunk. They live in `tapes/blank/` and
`scripts/make_blank_tapes.py` writes them byte-reproducibly from a table; its `--check`, which a
ctest runs, fails unless the committed files and the set match what it makes.

`CassetteService.ListBlankTapes` lists them by label in file name order with their lengths,
and `CreateTape` copies one into place on the server's host as `DiscService.CreateDisc` does a
disc: a destination without an extension gets `.uef`, another extension is refused, and an
existing destination is refused unless `overwrite` is set. The CLI's `list-blank-tapes` and
`create-tape` do the same (see [cli.md](cli.md)). Creating a tape and inserting it are separate
steps, and the new tape is recordable because it is a UEF in a file the user can write.

A template is never a tape: `InsertTape` refuses a file in the server's own blank tapes
directory, compared as canonical paths, with `INVALID_IMAGE` and a sentence pointing at
`create-tape`. Recording on a template would change every tape made from it afterwards. The
source tree's `tapes/blank/` is not the server's business; a client keeps the user from saving
over a template (client specification 9.0.1).

## CassetteService

`CassetteService` (`src/service/proto/cassette.proto`, implemented in
`service/CassetteService.hpp`) is registered in every server beside `DiscService`.

| RPC | Behaviour |
|---|---|
| `InsertTape(url, channel)` | Opens a UEF, CSW or WAV image the server can reach, given as a `file://` URI or a path (a relative path is taken from the server's working directory) and judged by content, and leaves the deck stopped at the start. `channel`, `"left"` or `"right"`, chooses a stereo WAV's; without it the louder plays; any other value is refused with `INVALID_FORMAT` ("A WAV's channel is left or right, not X."). The blank-template check comes before the file is opened. Returns the tape's metadata. The file is read on the gRPC thread; the insertion is made under pause |
| `StreamAudio(stream of AudioInputChunk)` | The live tape. The first message must be an `AudioInputFormat`: `sample_rate` 8000 to 192000 Hz, `sample_format` S16 or F32, `channels` 1 or 2, the `channel` to take from two (`"left"` or `"right"`, the left when absent) and a `device_name`; then `samples`, little-endian. A valid format inserts the live tape, stopped, named after the device or "Live input"; the client then calls `Play`. A first message that is not a format, a value out of range, or a second format is refused with `INVALID_FORMAT`; a tape already in, with `DECK_OCCUPIED` ("...eject it before streaming X."). Returns a `StreamAudioSummary`: `samples_received`, `samples_dropped` and `silence_samples_inserted`, with `error` and `kind` for a refusal (it has no `success` field). See Live input |
| `EjectTape()` | Stops the deck and removes the tape |
| `Play()`, `Stop()` | The deck's keys. Play at the end of the tape succeeds and leaves the deck stopped |
| `Record()` | Press record; refused with `READ_ONLY` for a tape that cannot be recorded on (a CSW or WAV, a UEF with an &0113 chunk, a UEF held only in memory, or a UEF whose file or folder the server cannot write), with the sentence `TapeMetadata.record_refusal` carries |
| `Rewind()`, `FastForward()` | Wind at 30 times real speed, ungated by the relay, until stop, play, seek, eject or an end of the tape |
| `Seek(position_seconds)` | Moves the tape at once and ends a wind; a negative or NaN position is refused with `INVALID_POSITION` (checked before the deck is, so a bad position on an empty deck is this, not `DECK_EMPTY`), and one beyond the end lands at the end |
| `GetDeckStatus()` | `DeckStatus`: `has_cassette_interface`, `tape_inserted`, `tape` (a `TapeMetadata`: `name`, `url`, `format` as `UEF`, `CSW`, `WAV` or `LIVE`, `title`, `origin`, `target_machine`, `duration_seconds` to the millisecond, `recordable` and `record_refusal`, a WAV's or stream's `sample_rate` and, for a stereo one, the `channel` played), `transport`, `position_seconds`, `motor_on` (the relay), `ula_part`, `live_ring_fill`, `live_overruns` and `live_underruns` for a live tape, `signal_map_ready`, `signal_map_generation` and `append_refusal` |
| `GetTapeSignalMap()` | The tape's signal map, a level a second; see Signal map |
| `SeekToAppendPoint()` | Seek to two seconds past the last recording, returning the position and whether it was at the end; see The append point |
| `ListBlankTapes()`, `CreateTape(label, destination_path, overwrite)` | The blank tapes, and a copy of one into place; see Blank tapes |
| `SubscribeDeckStatus(min_interval_ms)` | The status at once, then on every change of presence, transport, relay, tape, position by seek, signal-map readiness or generation, or append refusal, and while the deck plays or records the position, at most every `min_interval_ms` of emulated time (0 means the default, 100); while it winds, every thirty times that much tape; while a live stream is in, whatever the transport or relay. The handler polls the deck's atomics every 5 ms of wall time |

Every unsuccessful request returns `success = false`, a sentence naming the tape by its file
name, and a `CassetteErrorKind`; `CreateTape` has its own `CreateTapeErrorKind` (below), and
`StreamAudio` reports through its summary:

| Kind | When |
|---|---|
| `NO_CASSETTE_INTERFACE` | The machine has no cassette interface |
| `CANNOT_OPEN` | The file could not be found or could not be opened |
| `UNREADABLE` | The file is empty or could not be read, its gzip is damaged, a chunk or header is truncated, or any other error while opening |
| `UNSUPPORTED_FORMAT` | It is not a UEF, CSW or WAV tape image, a gzip of something other than a UEF, or a WAV of an encoding, bit depth or channel count Beebium does not play |
| `INVALID_IMAGE` | It is a UEF, CSW or WAV whose contents break the format, or a UEF that holds a disc, ROM or machine state, or it is one of the server's blank tape templates |
| `DECK_OCCUPIED` | A tape is already in, on `InsertTape` or `StreamAudio`; the sentence names both tapes |
| `DECK_EMPTY` | Play, stop, record, rewind, fast forward, seek, eject, append or a signal map with no tape in |
| `INVALID_POSITION` | A seek to a negative or NaN position |
| `READ_ONLY` | Record on a tape Beebium only reads (a CSW or WAV, a UEF with an &0113 change of base frequency, a UEF held only in memory, or a UEF whose file or folder the server cannot write) |
| `LIVE_SOURCE` | Seek, wind, record or append on a live tape |
| `INVALID_FORMAT` | A live stream's format, or a WAV channel other than left or right, refused |
| `NOT_READY` | `SeekToAppendPoint` before the tape's signal map is computed |

For example: "The deck already holds game.uef; eject it before inserting other.uef."

`CreateTape` answers with `success`, the path written, and on refusal a sentence and a
`CreateTapeErrorKind`: `UNKNOWN_LABEL` (no template of that label), `DESTINATION_EXISTS` (and
`overwrite` was not set), `WRONG_EXTENSION` (the destination has an extension other than
`.uef`) or `WRITE_FAILED`. The sentences live in `cassette/BlankTapes.hpp`.

The field `DeckStatus.tape_inserted` says whether a tape is in. It is not called `has_tape`
because protobuf derives `has_tape()` from the `tape` field.

The clients mirror the service. The Python client provides `bbc.cassette` and
`Beebium.boot_tape`, and the TypeScript client provides `Beebium.cassette` and `bootTape`. Each
boot call inserts the tape, types `*TAPE` and a load command (`CHAIN""` unless told otherwise),
and presses play; the Python one first makes sure the machine is running. The deck objects
mirror every RPC:

| Python `bbc.cassette` | TypeScript `cassette` |
|---|---|
| `insert(path_or_url, channel=None)`, `eject()`, `play()`, `stop()`, `record()`, `rewind()`, `fast_forward()`, `seek(seconds)` | `insert`, `eject`, `play`, `stop`, `record`, `rewind`, `fastForward`, `seek` |
| `seek_to_append_point()` returning `AppendPoint` | `seekToAppendPoint()` |
| `status()` returning `DeckStatus`; `events(min_interval_ms=100)`, the status stream | `status()`; `subscribe(minIntervalMs=100)`, an async iterator |
| `signal_map()` returning `SignalMap` (`ready`, `seconds`, `levels: bytes`) | `signalMap()`, with `levels` a `Uint8Array` |
| `list_blank_tapes()` returning `BlankTape`s; `create_tape(label, destination_filepath, overwrite=False)` returning the path | `listBlankTapes()`; `createTape(label, destinationFilepath, overwrite=false)` |
| `stream_audio(samples, sample_rate, sample_format="f32", channels=1, channel=None, device_name=None)` returning `StreamAudioSummary`; `stream_wav(wav_filepath, realtime=True, channel=None, block_seconds=0.01)` | `streamAudio(...)`; `streamWav(wavFilepath, {realtime, channel})` |

A refused request raises `CassetteError` with its `CassetteErrorKind`, and a failed create
`CreateTapeError` with its `CreateTapeErrorKind`; the dataclasses (`TapeMetadata`,
`DeckStatus`, `AppendPoint`, `SignalMap`, `BlankTape`, `StreamAudioSummary`) and enums live in
`beebium.client.cassette` and are exported from the TypeScript package's index.

## Signal map

The deck's position timeline is coloured by where the tape carries signal: grey where it is
silent, the accent colour where something is recorded, whether or not it decodes. It shows where
the tape is quiet, not what is on it. The signal map (`cassette/SignalMap.hpp`) is one level per
second of tape, 0 to 255: the fraction of that second lying within a run of edges.

- **A run** is a sequence of edges each less than 4 ms after the last (`SIGNAL_RUN_SPACING`,
  4923 ULA clocks; a Decision), from its first edge to its last. A `Gap` item, or any longer
  spacing, ends one. A 300 baud zero's half-cycle is 1.67 ms, so every legitimate tone is a run,
  carrier included: carrier is signal, since a person looking for a recording looks for where
  its leader begins.
- **One walk for every kind of tape.** `compute_signal_map` walks the `TapeSignal` the deck would
  play, from its start to its end. A WAV goes through the front end, so its silence is what the
  comparator's hysteresis rejects: a recording's noise floor, hum below the high-pass, or
  nothing. There is no separate amplitude threshold.
- **Length.** The map has as many seconds as the duration the deck reports (to the
  millisecond), rounded up. A second wholly within a run is 255, and one wholly outside is 0.
  The last second's fraction is of the part of it the tape holds.

### When it is computed, and by whom

The deck owns a `SignalMapper`, a worker thread of its own. It starts with the first job and
lives as long as the deck. The worker walks a `TapeSignal` opened afresh on the same image, never
the one the emulation thread plays:

| When | The image walked |
|---|---|
| A tape with a file is inserted | the file, reopened on the worker (a stereo WAV on the channel played) |
| A UEF with no file is inserted | its encoded image |
| A recording ends | the image the deck encodes then: the same bytes it writes back, encoded once |
| A live tape is inserted | none: it has no duration, and `ready` stays false |
| A tape Beebium only reads is inserted with no file (only tests do this) | none |

The emulation thread, or a service thread with the emulation paused, only posts a job: for a
tape with a file, the worker reopens the file itself, so the inserting thread pays nothing for
the map; for a file-less UEF the posting thread encodes the image first, and when a recording
ends the emulation thread encodes it, the same bytes the write-back writes. The latest job wins:
posting one, an eject, or a recording ending cancels a walk in progress, and a superseded walk
never publishes. A walk checks for cancellation once per item of its tape, and no item costs
more than a block of a WAV's samples: a `SampledTape` returns a `Gap` after 4096 samples with no
edge, rather than scanning a whole silence in one `next()`. That also bounds what one `next()`
costs the emulation thread when it plays a WAV. An eject during a walk of the longest corpus take
returns in under 0.1 ms. The mapper is the deck's last member, so it is destroyed first: its
destructor cancels the walk and joins the worker. No walk outlives the deck, and a job's opener
owns what it reads (a file path, or a shared copy of the bytes), never the deck's tape. The map is
not persisted; it is recomputed on each insert.

### While recording

A map that changed only when a recording ended would show nothing new until the user stopped or
scrubbed. So while the transport is Recording, the deck keeps the map current in the head's wake
itself, from the edges and clocks it records, with the same run rule:

- A second the head has finished is final: 255 for tone, 0 for silence it erased, the fraction
  for a mix.
- The second under the head reads the fraction of its passed part that lies in a run.
- Seconds ahead of the head keep their levels.

The first second of a recording counts only the part the head has passed since recording began.
The wake updates a map that is already ready; a recording begun before the tape's first map
has been computed publishes nothing until the walk at its end. The deck republishes the wake,
with `signal_map_generation` one on, at most once per second of
tape time (`SIGNAL_MAP_RECORDING_PUBLISH_CLOCKS`, a Decision), so a client refetches the map's few
kilobytes once a second. When the recording ends, the walk of the image written back replaces the
wake as the authority, one generation further on, and the wake's map stays published until it
does.

This runs on the emulation thread. Each recorded clock costs two comparisons, each written edge a
little arithmetic, and each publish a copy into the mapper under its mutex.

### Cost

On a developer Mac (Apple silicon), Release build, with each WAV read in place from the corpus:
- Plan B2 side 1 at normal level (55 MB, 303 s at 48 kHz): 0.24 s.
- The longest Cascade 50 take (313 MB, 1710 s): 1.35 s.
- Across the whole corpus the throughput is about 230 MB, or 60 million samples, a second.
- A UEF or CSW walk takes milliseconds.

The walk runs on the deck's worker, so it costs latency to the map, not emulation time.

The 4 ms threshold sits in an empty valley. Over Plan B2, fewer than ten edge spacings lie
between 1 and 12 ms; over a Cascade 50 take, about seventy, against millions of edges inside
runs. Every take of a side finds the same silences at every recording level. Each Cascade 50
side-one take, for example, has 29 quiet stretches, the 4 to 8 s gaps between its programs,
which read 0 throughout.

### The RPC and the clients

`GetTapeSignalMap()` returns `TapeSignalMap`: `success`, `error` and `kind` as every cassette
response does (`DECK_EMPTY` with no tape, `NO_CASSETTE_INTERFACE`), then `ready`, `seconds` and
`levels`, one byte a second. It is a separate RPC, not a status field, because the status
streams many times a second and the map is kilobytes (a C90 is 5400 bytes). `DeckStatus` carries
`signal_map_ready` and `signal_map_generation`, which counts the maps published. The status
stream sends a status when either changes, so a client knows when to fetch the map, and when to
fetch it again after a recording. The Python client has `bbc.cassette.signal_map()`, returning a
`SignalMap` (`ready`, `seconds`, `levels: bytes`). The TypeScript client has
`cassette.signalMap()`, with `levels` a `Uint8Array`. The GUI clients draw it on the position
bar: client specification 9.0.

## The append point

A key that takes the tape to just past its last recording, so the user can add to a tape without
scrubbing for the end of the blue: the not-quite mirror of seeking back to the beginning.

- **The rule** (`signal_map_append_second`, `cassette/SignalMap.hpp`): the end of the last
  second of the signal map whose level is not zero, plus `APPEND_MARGIN_SECONDS` = 2 (a
  Decision). With the last loud second at index L, the point is L + 1 + 2 seconds. The last
  edge lies somewhere in second L, so the next leader starts 2 to 3 s after it: the gap a careful
  user leaves, so one recording's leader does not butt against the last block before it. A map
  silent throughout gives 0, where Back to the beginning goes. Signal is what the map says:
  hum above the comparator's threshold reads as recorded, as a deck's meter would. Long gaps
  between programs do not fool it, since it finds the last recording, not the first gap.
- **The seek** (`CassetteDeck::seek_to_append_point`) is the deck's own seek to that point. A
  recording or a wind ends first, a playing deck plays on, and a point at or past the end of the
  tape lands at the end and stops the deck, with `at_end` said: there is no room.
- **A recording just made counts.** Ending a recording flushes the wake into the map up to the
  head, including what was recorded since the last once-a-second publish (see Signal map, While
  recording). So the seek uses the whole recording at once, without waiting for the walk of the
  image written back. The flush publishes no generation; the walk that follows publishes one.
- **Refusals**, in-band: `DECK_EMPTY` with no tape; `LIVE_SOURCE` for a live tape ("A live input
  has no end to append to."); and `NOT_READY` before the map is computed ("The tape's signal map
  is not ready yet; try again in a moment."). `DeckStatus.append_refusal` carries the sentence the
  seek would refuse with now, empty when it would be accepted. The status and the RPC take it from
  one place, so a client shows the server's reason on a greyed key and never words it itself.

`CassetteService.SeekToAppendPoint` performs it and returns `position_seconds` and `at_end`. The
Python client has `bbc.cassette.seek_to_append_point()`, returning an `AppendPoint`, and the
TypeScript client has `cassette.seekToAppendPoint()`.

## The motor LED

`CassetteMotorLed` (`cassette/CassetteMotorLed.hpp`) registers the indicator
`cassette-motor-led` in every hardware policy, after the lock LEDs and before the disc drives,
so clients list it between SHIFT LOCK and FDD 0, as the storage sidebar puts the cassette above
the floppies. Its label is "CASSETTE MOTOR", its color 625 nm (red) and its shape domed. It reads 255 while latch bit 7 is set and
0 while it is clear, whether or not a tape is in. It passes through the indicator queue
unfiltered, so the MOS's 1 ms blips reach the client. The client applies whatever hold-off it
applies to the drive LEDs. See [indicators.md](indicators.md).

## Presets and machine directories

A preset names a tape as it names a floppy:

```json
"storage": {
  "cassette": { "image_uri": "game.uef" }
}
```

`image_uri` takes a path relative to the preset, a `file://` URI, a bare name, or `null` (the
default). A bare name is searched for in the preset's container and then in the per-user
`tapes/` directory, which sits beside `discs/` and `roms/` (see [deployment.md](deployment.md)).

A tape the template names by search, or holds within itself, is copied into the machine
directory, as such a floppy is. A tape named by a path elsewhere is opened in place.

A preset's tape is inserted at launch with the deck stopped.

The preset tools treat the key as follows:

- `resolve-preset` reports `image_uri` as a `file://` URI or `null`;
- a missing tape is a warning at `/storage/cassette/image_uri`;
- `describe-preset-schema` reports the deck in its storage section as
  `{"has_cassette": true, "label": "Cassette", "image_types": ["uef", "csw", "wav"]}`, and
  also `storage.builtin.cassette: true`, the older form the first clients read;
- a preset's tape on a model with no cassette interface is ignored with a warning at
  `/storage/cassette/image_uri`;
- `create-preset` and `create-machine` record `--tape` as `storage.cassette.image_uri`.

See [preset-schema.md](preset-schema.md).

A saved machine keeps two things apart:

- which tape it has is configuration, in its `preset.json`;
- where that tape is, is state, in `machine.json` as `"cassette": {"position_seconds": 12.5}`.

`SaveMachine` writes the position only while the deck holds the configured tape. It omits the
position otherwise, which clears an earlier one; when it is absent the position is 0. The
position is never written at shutdown. `start --machine` inserts the tape and puts it back at
that position, stopped. A tape inserted at run time is not recorded, just as a floppy inserted
at run time is not. See [machine-directory.md](machine-directory.md).

## `--tape`

`--tape <filepath>` inserts a tape at launch, UEF, CSW or WAV, and plays it, unlike a preset's
tape. It is anchored as `--floppy` is: a relative path is taken from the working directory, and
a `file://` URI is accepted. It wins over a preset's `storage.cassette`. Startup reports what
was done, as `Loading tape: <path> (playing)`, `(stopped)` or `(stopped at N s)`.

A tape that cannot be found or opened ends the server with exit status NOINPUT (66). A file
that is not a tape image, an empty file, or a URL that is not local ends it with CONFIG (78)
and the reader's sentence. See [cli.md](cli.md).

## Testing

The tests build their tapes when they run; no tape image is typed into the Beeb.

- In C++, `tests/support/tape_builder.hpp` writes UEF and CSW of MOS blocks, with the MOS's
  CRC-16/XMODEM and timings.
  It also writes WAVs of a tape's signal, each half-cycle as a sine, a square or a sine shifted
  90°, with noise, a second channel and every sample format.
- In Python, `clients/beebium-python-client/tests/tape_builder.py` does the same, independently
  of the C++ build.
- BASIC programs are tokenised from `.bas` sources in `tests/assets/cassette/`.

| Suite | What it covers |
|---|---|
| `tests/test_mc6850.cpp` | Real division at /1, /16 and /64; start-bit detection, mid-bit sampling and short-glitch rejection; transmit timing and the idle-TDR rule; every word format; framing and overrun errors; master reset clearing the control register and holding TxD at mark mid-character; the /DCD latch and its clearing on a data read; the receiver held idle, RDRF clear, while /DCD is high |
| `tests/test_serial_ula.cpp` | The 16/13 MHz clock and its dividers; 64 RS423 clocks a bit at every rate; RTS and BREAK forwarding; transmit and receive shifting and loopback; the motor bit; the read of &FE10, and peek having no side effect; cassette mode's /DCD, /CTS and silence; the part |
| `tests/test_serial_ula_cassette.cpp` | The separator in ULA clocks: bursts, timeouts and the data rule. A tape byte arriving only at the matching ACIA divider. The detector: threshold for each part, re-arming by a 0, by motor off and by silence, and surviving latch writes. The VLSI inversion. The synthesiser's tones and silence; TDRE with no sink. The deck's movement, its refusal of a second tape, and Break. Winding: rewind from 60 s reaching 0 in 2 s and fast forward of a 90 s tape reaching its end in 3 s, with the relay open and no edges; stop and play mid-wind; a seek ending a wind |
| `tests/test_tape_recording.cpp` | The classifier, chunk by chunk (carrier and its split, a 900 s silence, `&0112`, an 8N1 byte, a 300-baud byte, `&0104`, 7E1 as 8N1, `&0102`, metadata first); round trips of a built tape, plain and gzipped, and of a recording with its stretch; recording silence over carrier; recording to the end; a recording through the deck read back to the clock; what ends a recording; write-back on stop, eject, flush and the deck's end, never for an unrecorded tape, and a failed write leaving the file whole; nothing lost when the relay opens; `record()` refused on a CSW, an empty deck, a UEF held only in memory, a UEF with &0113, and a UEF on a read-only file or in a read-only folder; each blank tape's length, silence and recordability |
| `tests/test_tape_signal.cpp` | Every UEF chunk the reader renders, refuses or skips; gzip; metadata; seek and duration; CSW against the same tape's UEF to within one clock, both versions, polarity, long pulses, and the refusals. WAV: every sample format, the refusals, the louder channel and a chosen one, reading from the file and seeking, and a UEF rendered as 90° sines playing as the same half-cycle classes |
| `tests/test_front_end.cpp` | The model's constants; its phase at 1200 and 2400 Hz; 50 Hz and 8 kHz rejection; the comparator at 0.08 and 0.01, a full-scale square, noise alone and under a sine; interpolated edges 256 ± 2 clocks apart at 48 kHz; the same edges within 2 clocks at 22050, 44100, 48000 and 96000 Hz |
| `tests/test_cassette_mos.cpp` | With MOS 1.20 as a test vector: `*TAPE` and `CHAIN""` loading and running a BASIC program from UEF, at 300 baud after `*TAPE3`, and from CSW. `Data?` and `Rewind tape` for a bad data CRC; `Block?` for a block out of sequence. The Welcome tape's short leaders. `*CAT` with the tape running only while the relay is on; searching until the deck plays. `SAVE` to a copy of the C15 blank with the deck recording, at 1200 baud and at 300, `CHAIN`ed back in the same machine and a fresh one, and the written file's chunks. `tape_ok` as a WAV: 90° sines, sines at 0.18 with noise at 0.02, and square waves load; 0° sines do not (the model's prediction). A SAVE whose sync byte was taken at once keeps its header whole; a SAVE ending in a stray &FF frame still loads; a recording's signal map shows where it landed |
| `tests/test_tape_signal_map.cpp` | The signal map: a builder UEF of gaps, carrier and blocks to its levels, including half-second boundaries; a 300 baud block as signal throughout; the same tape as a WAV within 16 a second, clean, with noise at 0.01 and with 50 Hz hum at 0.5 in its gaps, and as a CSW within 2; a superseded job never publishing; clearing or destroying the mapper stopping a ten-minute WAV walk within 100 ms; the deck mapping a CSW and a stereo WAV from their files and a UEF from its image, none for a file-less CSW or a live tape, and forgetting the map on eject; while recording, completed seconds exact in the head's wake, the second under the head its passed part, seconds ahead untouched, five to seven publishes over six seconds and one more for the walk at the end, and silence recorded over carrier erasing it in the wake; the append point's rule (a tape recorded to 10.3 s gives 13 s, a blank 0, a lone loud second passed, a full tape past its end) and the deck's seek to it, playing on, stopping at the end, and counting a recording just ended, its last unpublished part included |
| `tests/test_grpc_cassette.cpp` | `CassetteService`: status on a fresh machine; a stereo WAV's rate and its louder or chosen channel; insert, refusal of a second tape, and the error kinds; winding at 30 times real speed with the motor off; play gated by the relay; the status stream; a BASIC program loaded through MOS 1.20; `Record` on an empty deck, a CSW and a UEF, the file rewritten on `Stop`; the blank tapes listed, created, refused and recorded on. `StreamAudio`: its refusals; a live tape inserted, played, refusing what it cannot do, carrying its fill and counts; a stream that ends playing out its ring when the deck plays and going at once when it is stopped; `EjectTape` ending a stream; a live tape reporting a channel only for a stereo stream; MOS 1.20 loading tape_ok streamed in real time to a machine at real speed, with and without the relay blipping during the leader. `InsertTape` refusing a blank-tape template; `record_refusal` matching Record's sentence for a CSW and a read-only UEF; recording through the service race-free against a status stream and the keys (with a hidden `[.][race]` control). `GetTapeSignalMap`: `DECK_EMPTY` with no tape, a UEF's levels and seconds once ready, the generation counting, a live tape never ready, and the status stream reporting the map ready; during a MOS SAVE at real speed, the generation advancing in the status stream and the map fetched within a generation unchanged. A SAVE to a tape made with `CreateTape` landing in that tape, every template unchanged. `SeekToAppendPoint`: each refusal matched beforehand by `append_refusal`, a recorded tape landing at 13 s and playing on, a full tape at its end; and MOS 1.20 SAVEing two programs one after the other with it, both listed by `*CAT` and the second loading with `CHAIN` |
| `tests/test_blank_discs.cpp` | `list-blank-tapes` and `create-tape` over the committed templates, beside the disc subcommands |
| `tests/test_live_source.cpp` | The ring: a tone paced through it with no runs and its edges 256 clocks apart; priming a late stream, starving and priming again; an ended stream played out without priming; silence counted when a stream stops; a stream 10% fast dropping at the excess rate, the oldest; a producer and consumer on two threads; S16 and F32, mono and either stereo channel, in chunks that split frames, giving the same edges; the deck's rules for a live tape |
| `tests/test_grpc_indicator.cpp` | Every model lists the motor LED between the lock LEDs and the drives, with its label, colour and shape (a template test over the hardware policies); over gRPC on a Model B, `ListIndicators` and the LED following latch bit 7, waiting for each transition |
| `tests/test_preset_subcommands.cpp` | `resolve-preset` and `describe-preset-schema` for the deck; a preset's tape inserted stopped; `--tape` winning and playing; a bare name found in `tapes/`; a missing tape a warning at its `image_uri`; no tape resolving to null |
| `tests/test_cli.cpp` | `--tape` setting the start options; `describe-preset-schema` reporting the built-in cassette |
| `clients/beebium-python-client/tests/test_cassette.py` | `bbc.cassette` round trips and winding, errors with kinds and sentences, the status stream, `boot_tape` running a tokenised BASIC program, a WAV's rate and channel, blank tapes listed and created and a template never inserted, record as a transport with a CSW read-only and a read-only tape saying why, `signal_map` of a tape and of a recording, and `seek_to_append_point` landing and refused |
| `clients/beebium-python-client/readme/snippets/test_readme_cassette.py`, `test_readme_cassette_recording.py` | The README's snippets run: insert, play, seek, wind and eject; and a SAVE to a new C15 CHAINed back |
| `clients/beebium-python-client/tests/test_cassette_live.py` | `stream_audio`'s summary; a live tape's refusals; the refusals of a stream; `stream_wav` of tape_ok in real time loading through MOS 1.20 |
| `clients/beebium-python-client/tests/test_saved_machines.py` | A saved machine resuming its tape where it was saved, and a run-time tape not recorded |
| `clients/beebium-typescript-client/tests/cassette.test.ts`, `cassette-integration.test.ts` | The TypeScript mirror, with mocked stubs and against a server |

The GUI clients test their decks against the client specification, each in its own suite:

| Suite | What it covers |
|---|---|
| `clients/linux/tests/test_cassette.cpp` | The Linux deck model and widget: status mirroring, the keys and their latching and colour, errors and the Eject offer, blank tapes, the timeline coloured by the signal map, To the end of the recordings, recent tapes; and the live input: the device's format to the stream, 10 ms chunks of whole frames, the live tape in and out, the Input Device menu and its channel choice, the keys a live tape greys, the starve message once with its silence made up, a device change or disconnection ending the stream over a starve, and the level meter in the timeline's place |
| `clients/linux/tests/test_cassette_recording.cpp` | Against a real server: a SAVE lands in a new blank tape and never its template; a recorded tape is offered among recent tapes and CHAIN loads from it |
| `clients/linux/tests/test_live_starve.cpp`, `test_live_level.cpp` | The starve detector's three-second rule, re-raise and sentences; the level meter's decibels, fall rates, hold and clip mark |
| `clients/linux/tests/test_client_settings.cpp`, `test_configuration_sections.cpp` | Recent tapes kept apart from discs; the Cassette row of the New Machine configurator |
| `clients/macos/Beebium/BeebiumTests/CassetteClientTests.swift` | The status stream and stale handling; a live tape's ring state; a sustained underrun raising one message, sub-second snapshots not counted, a device change outranking a same-second starve and resetting the tracking |
| `.../CassetteDeckFormattingTests.swift`, `RecentTapesTests.swift`, `CassetteConfigTests.swift`, `CassetteErrorKindLabelTests.swift`, `CreateTapeErrorKindLabelTests.swift` | The counter, the keys, the to-end key's enabling and help, what a live tape greys, the signal map's columns; recent tapes; the draft's `storage.cassette.image_uri` and the schema; the error-kind labels |
| `.../CassetteRecordingIntegrationTests.swift` | Against a real server: recording a new blank tape keeps the templates pristine, and the recording reloads |
| `.../LiveAudioInputTests.swift`, `LiveStarveDetectorTests.swift`, `LevelMeterStateTests.swift` | The device format to the stream format, 15 ms chunks, S16 and F32 encoding, the peak and clip run of the taken channel, a rate or channel change and a disconnection; the starve detector's baseline, three-second rule, re-raise and detail text; the meter's decibels, target tick, fall rates, hold and clip mark |
| `.../AudioEngineOutputTests.swift` | The sound output unit is Apple's default output, so audio playback never opens an input and never prompts for the microphone |

`clients/beebium-python-client/tests/test_cassette_corpus.py` loads two collections from the
directory `BEEBIUM_TAPE_CORPUS_DIRPATH` names, and skips when it is unset or not a directory.
First, commercial tapes as UEF or CSW, each chosen because it exercises a particular part of the
path:

- the /DCD latch and its clearing;
- short carrier tones;
- the motor toggling during carrier;
- tone and data with no gap between them (the one CSW);
- the ACIA's master reset clearing the control register;
- writes to the ACIA during and after a load.

Each is identified by its SHA-256, and skipped when no file matches. Second, a corpus of recordings supplied for the project, WAV
files of real tapes played on a deck, named by file, each with a TOML beside it naming the signal
channel:

- Plan B2, side 1: takes 001, 002, level-normal and level-high reach the game's title screen with
  no `Data?` or `Block?`; the level-low take is a known failure (below), asserted as one;
- Cascade 50, side 1, take 1, whose TOML warns of bad patches (`Data?` in GAL and PLA): `CHAIN
  "LUN"` loads the program that lies past them;
- Plan B2, side 1, take 001, streamed live in real time through `stream_wav` (marked slow: it
  takes as long as the tape), with no overrun;
- Palace of Magic, side 1, the partial take with a play-stop-rewind-play in it: seeked past the
  rewind, found in the WAV as the longest stretch without tone and asserted to lie between 90
  and 100 s, the loader loads from the replay and the game's blocks follow.

No commercial tape or recording is committed, and a test skips when its file is absent.

## Decisions still unverified

These follow the programme specification. They are to be checked on hardware or against the
corpus:

1. The missing-edge timeout of 284 µs.
2. The detector's count waiting through silence with the motor on, until the next edge clears
   it.
3. The one-cell /DCD pulse for the Ferranti, in place of an edge-aligned half-cycle.
4. The ACIA's /DCD latch clearing on a data read alone, and master reset clearing the control
   register.
5. A read of &FE10 acting as a write of &FE.
6. The &0114 bit order.
7. The instantaneous motor, with no inertia.
8. The front end's constants as a group: the 20 Hz DC block, the 1.29 kHz and 4.13 kHz corners,
   the stage gains of 1.27 and 1.26 behind their Q, and the hysteresis of 0.02 of full scale.
9. Whether 0° sines fail to load on a real machine, as the model predicts.
10. The low-level Plan B2 take. It does not load through the model: 261 `Data?` over 300 s.
    Its input peaks at 0.18 (RMS 0.054) and its filtered signal at 0.065; the normal take's at
    0.78 and 0.40. The half-cycles through the front end over the first 120 s, in 32-clock
    buckets:

    | Take | 192 | 224 | 256 | 288 | 320 | 352 | 416 | 448 | 480 | 512 |
    |---|---|---|---|---|---|---|---|---|---|---|
    | low | 4789 | 154476 | 89317 | 17846 | 17812 | 1046 | 10461 | 26250 | 25902 | 21586 |
    | normal | 29 | 174197 | 74555 | 35887 | 879 | 0 | 10903 | 14733 | 36240 | 22552 |

    The hysteresis does not explain it alone:

    | Hysteresis | `Data?` over 300 s | Half-cycles at 352-383 | 50 Hz test |
    |---|---|---|---|
    | 0.02 (the model's) | 261 | 1046 | passes |
    | 0.015 | 237 | 64 | passes |
    | 0.01 | 85 | 3 | fails |

    The suspected mechanism: with the hysteresis at a third of the filtered peak, the comparator's
    crossing delay, asin(h / A), differs between the two tones because the filters pass 0.51 and
    0.71 of them, which shifts every half-cycle at a change of tone by tens of clocks on a quiet
    take and by almost nothing on a loud one; that, with the dropouts of a quiet recording, is a
    plausible account. The real machine's gain-50 clipper has a far smaller hysteresis relative to
    the signal than the model's. Whether the take loads on a real Beeb is not yet known, and no
    constant moves on one take.
11. The live source's ring of two seconds and its priming level of a quarter of a second
    (`LiveSource::RING_SECONDS` and `PRIME_SECONDS`).
12. The time from a write to the 6850's TDR to the start bit on TxD, with the transmitter idle:
    the model takes one full bit time (the write restarts the bit-rate counter), the datasheet
    says "within 1-bit time". With a real machine and a logic analyser, at 1200 baud: trigger on
    the write to &FE09 while TxD idles at mark, and measure to TxD's falling edge. Anything
    under about 60 µs would mean MOS 1.20's block writer (which places the next byte about
    115 cycles after sending the sync byte) could send the sync byte twice. Tracked as issue
    #291, which sets out the measurements.
13. The signal map's run spacing of 4 ms (`SIGNAL_RUN_SPACING`): an edge further than that from
    the last ends a run, so a second is silent where no tone is.
14. Republishing a recording's wake at most once per second of tape time
    (`SIGNAL_MAP_RECORDING_PUBLISH_CLOCKS`).
15. The append point's margin of two seconds after the last second of signal
    (`APPEND_MARGIN_SECONDS`).

Known simplification, not a Decision: a recording written back to a UEF can be a few clocks
longer than the tape in the deck. A recorded C15 comes back 11 clocks (9 µs) over 900 s,
because the writer's &0116 gap chunk holds its length as a 32-bit float. The deck reports the
duration to the millisecond, and the signal map counts its seconds from that, so neither shows
it.

## Not yet built

- Recording to CSW or WAV.
- Selecting the VLSI part from a preset; every machine has the Ferranti.
- Write-back of a UEF that changes its base frequency (&0113); such a tape is read only.
- Resampling or clock recovery for the live input; a live load is for real speed.
- A Windows GUI client's deck and live input (the headless server and the Python and TypeScript
  clients serve Windows).
