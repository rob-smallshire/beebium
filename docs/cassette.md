# The cassette interface

The BBC Micro's cassette interface as Beebium builds it. It covers:

- the Serial ULA's cassette side: the data separator, the high-tone detector and the tone
  synthesiser, all clocked at 16/13 MHz;
- the MC6850 ACIA that divides the separator's clocks and latches /DCD;
- the tape sources: UEF and CSW images, and WAV recordings through a model of the machine's
  analogue input;
- the deck that plays them, and records on a UEF;
- the tape's signal map, where it carries signal a second at a time;
- the blank tapes the server ships;
- `CassetteService`, the motor LED, presets and machine directories, and `--tape`.

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
- `cassette-round9-signal-map-specification.md`: the signal map.

The serial side of the same chips, RS423, is in [serial-acia.md](serial-acia.md).

## What is modelled and what is not

| Modelled | Not modelled (yet) |
|---|---|
| The ULA clocked at 16 MHz / 13 from a fractional accumulator on the 2 MHz tick, so every interval is a whole number of ULA clocks and the rates run 0.16% above nominal, as on the machine | The real part's 20 to 25 µs delay from an edge to its burst of receive clocks (not observable by software) |
| The data separator: a burst of four receive clocks at each edge, a 350-clock missing-edge timeout, the data rule from the gap just ended | Error correction: there is none on the machine either; a dropout reads as low tone |
| The high-tone detector: a counter of 256-clock cells, a one-cell /DCD pulse at 962 cells (Ferranti) or 445 (VLSI), and the rules that re-arm it | The Ferranti's edge-aligned pulse, its frequency acceptance threshold (about 1790 Hz), and the chip-to-chip spread of its 200 ms |
| The tone synthesiser: 2403.8 Hz for mark, 1201.9 Hz for space, phase-continuous, sampling TxD at each 1024-clock cell boundary | The staircase output levels and the VLSI part's opposite polarity |
| The ACIA dividing real receive and transmit clocks by 1, 16 or 64, and the /DCD latch | |
| The Ferranti part (the default) and the VLSI part, including the VLSI's tone inversion on latch bit 3 | Selecting the VLSI part from a preset |
| UEF (plain or gzipped) and CSW (v1.01 and v2, RLE and Z-RLE) tapes, rendered as edges in ULA clocks | Live audio (round 8) |
| WAV recordings, squared by a model of the LM324 input stages: the DC block, the Sallen-Key high-pass and low-pass, and a comparator with hysteresis | The clipper's gain and the ULA input's threshold as such; board-to-board variation |
| One deck per machine: insert, eject, play, stop, rewind, fast forward, seek; the tape moving only while it plays with the relay closed, or while it winds | Motor inertia: the relay starts and stops the tape instantly. Tape audio: the tape reaches only the Serial ULA, and no client plays it |
| The motor relay as an indicator | |
| Recording on a UEF, written back to its file; four blank tapes | Recording on a CSW |
| A live audio input streamed by a client, through a primed ring and the same front end | Resampling or clock recovery: a live load is for real speed |

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

`SerialUla` (`serial/SerialUla.hpp`) is the ULA. `Mc6850` (`serial/Mc6850.hpp`) is the ACIA.

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
| Burst | four receive clocks of two ULA clocks each, starting at the edge |
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
The bit stays set until the data register is read. While /DCD is high the receiver is held
idle and RDRF reads clear.

### The tone synthesiser

With cassette selected and RTS low, the ULA samples TxD at each 1024-clock cell boundary
(832 µs). It fills the cell with two cycles of 2403.8 Hz for a 1 (mark) or one cycle of
1201.9 Hz for a 0 (space), phase-continuous, as edges to an attached `TapeSink`, or to the deck
while it records. RTS high or RS423 selected gives silence, and so does a master reset of the
ACIA, which holds TxD at mark: carrier. Otherwise the edges are discarded. Bytes still clock out
at the real rate, and TDRE returns when the ACIA says so, which some loaders observe.

### The part

`SerialUla` takes a part, Ferranti or VLSI, at assembly. Every Model B family machine has the
Ferranti part. The two parts differ in:

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
and asked its duration. Nothing in a source is called from anywhere but the emulation thread.

`open_tape_file` (`cassette/TapeImage.hpp`) opens a file and judges it by content, not by
extension. Its refusals are sentences naming the file, each with a `TapeImageError` kind:

- `CannotOpen`: the file could not be found, is empty, or could not be opened;
- `Unreadable`: the file could not be read;
- `UnsupportedFormat`: it is neither UEF nor CSW;
- `InvalidImage`: it is a UEF or CSW whose contents break the format.

### UEF

`UefTape` reads the `UEF File!` signature with major version 0 and any minor version, plain
or gzipped (it recognises the `1F 8B` magic). It renders each chunk as ideal square cycles at
the current base frequency. The default base is 1200 Hz, with carrier at twice it. Each cycle
is a low half then a high half, at the phase chunk &0115 sets. Because the cycles are ideal,
UEF loading does not depend on phase.

| Chunks | Treatment |
|---|---|
| &0100 | Bytes, 8N1 |
| &0102 | Explicit 1200-baud bits: the first byte the unused bits at the end of the last, then the bits least significant first, a 0 one cycle at the base, a 1 two at twice it |
| &0104 | Bytes in the chunk's own format: parity, and negative stop-bit counts. The parity swap of MakeUEF before 2.4 is applied when the &0000 origin string names it. Other than 7 or 8 data bits is refused |
| &0110 | Carrier |
| &0111 | Carrier, the dummy byte &AA, carrier |
| &0112 | A gap of n / (2 × base) seconds |
| &0113 | A change of base frequency, in the canonical meaning; outside 100 to 10000 Hz is refused |
| &0114 | Security cycles, least significant bit first, 1 a long cycle |
| &0115 | Phase |
| &0116 | A gap in seconds, as a float |
| &0117 | The bit encoding: 1200 (one or two cycles a bit) or 300 (four or eight); other values are refused |
| &0000, &0001, &0005, &0009, &0120, &0130, &0131 | Kept as metadata: origin, title, target machine |
| &0003, &0006 to &0008, &000A, &0101, &0103, &0105 | Ignored |
| &0200 and above | The file is refused with a sentence naming the chunk: it holds a disc, ROM or machine state, not a tape |
| Anything else | Logged and skipped |

The writer (below, under Recording) emits a subset of this table, so whatever it writes reads
back.

### CSW

`CswTape` reads v1.01 and v2, with RLE and Z-RLE compression, at any sample rate. It honours the
initial polarity flag and the &00 long-pulse escape. Each pulse becomes an edge at
pulse_length / sample_rate, in ULA clocks, through a rounder that carries the error forward. A
pulse longer than 100 ms is treated as a gap. A v1 file with a sample rate of 0 is refused.

### Sampled sources: WAV

A `SampledSource` (`cassette/SampledSource.hpp`) is a tape as sound: mono samples in [-1, 1) at
a rate, pulled in blocks. `WavSource` reads RIFF/WAVE files of 8-bit (unsigned), 16, 24 and
32-bit integer and 32-bit float samples, at any rate, mono or stereo, plain or
`WAVE_FORMAT_EXTENSIBLE`. It reads the file on demand through a block cache, so a WAV of any
length plays without being loaded whole. A stereo file plays the channel the client asks for
(`InsertTape`'s `channel`, `"left"` or `"right"`), or else the one with the higher RMS over the
first 10 seconds. ADPCM, mu-law and other encodings, more than two channels, and a data chunk
that runs past the end of the file are refused with a sentence naming the file.

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
internal rate of at least 88.2 kHz, with the input interpolated linearly between samples (a
symmetric kernel, so no phase): designed at 22.05 kHz, the bilinear transform's warping moved the
1200 Hz tone by 7 clocks. At 48 kHz the low tone's crossings move by +67° and the high tone's by
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

A client can stream an audio input on its host, a real cassette deck's line-out, to the deck
as the live tape, through `CassetteService.StreamAudio`. The samples go through the same front
end as a WAV's.

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
below real speed the ring overruns at the rate of the shortfall, above it underruns. The status
shows which.

`LiveTape` is the `TapeSignal` over the source and a `FrontEnd`, of format `LIVE`. It is paced by
emulated time: each item it gives is one sample's edge or a gap to its time. It has no end and
no length. The deck plays it whenever it is playing, whatever the relay: a deck without a remote
lead keeps playing and the real ULA keeps listening, and the relay still re-arms the high-tone
detector. Stopped, the deck takes the samples and they go nowhere, so the ring does not fill.
Seek, rewind, fast forward and record are refused with `LIVE_SOURCE`. The position counts the
samples taken since the stream began.

The live tape goes in, stopped, when a stream's format arrives, named after the device the
client gives. When the client closes the stream, a playing deck plays out what the ring holds and
the tape goes when it is empty, as a tape that runs out; a stopped deck lets it go at once.
`EjectTape` ejects it at once and ends the stream. The stream's summary gives the samples
received, dropped and made up as silence.

On this Mac a 48 kHz stereo WAV streamed live with the deck playing adds 0.35% of real time to
the emulation thread's work: the ring, the front end, and a tape item a sample.

## The deck

`CassetteDeck` (`cassette/CassetteDeck.hpp`) belongs to the hardware policy, beside the disc
drives. Every machine Beebium builds has one. Presence is a policy trait, `has_cassette`, so a
machine with an ACIA but no cassette interface can say no.

The deck holds:

- the tape: none, or one source with its name, URL, kind (UEF or CSW) and metadata;
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
  or at an end of the tape, where the deck stops. Play during a wind plays from where the
  tape has reached.
- **Seek** moves to a tape time at once; a seek to zero is the instant return to the start.
  A seek ends a wind and leaves a playing deck playing. A seek beyond the end lands at the end
  and stops the deck. At the end of the tape the transport stops; the tape does not loop.
- **Record** is a transport like play; see Recording below.
- **Eject** stops the deck and removes the tape.
- **Reset.** Break and power cycle leave the deck alone. The relay opens because the latch
  resets.

The deck publishes its state through atomics: presence, transport, relay, position and a tape
generation. While the deck plays, the position is published at most every 4096 ULA clocks.
The service reads these without stopping the machine.

## Recording

A UEF can be recorded on; a CSW is only read, and `record()` on one is refused with a sentence
naming the tape and its format. A UEF that carries an &0113 change of base frequency is read
only too, for now: the writer describes every tape at the nominal 1200 Hz base and would lose
the changes, so `record()` is refused with `READ_ONLY` and a sentence saying so. Recording is a transport, as on a real deck: the user presses
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
a move of its machine, and at shutdown, and only if it has been recorded on since it was last
written. The whole file is written beside the old one and renamed over it, gzipped if it was
gzipped. A file that cannot be written is left whole, the failure is logged in a sentence, and
the recording stays in memory to be written at the next of those moments.

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
| Silence | `&0112` when it is a whole number of 1200 Hz half-cycles to within two clocks; otherwise `&0116`, seconds |
| A run of short cycles | `&0110` carrier, at most 65535 cycles a chunk |
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
[deployment.md](deployment.md)): `Blank C15 (15 minutes).uef`, `Blank C30 (30 minutes).uef`,
`Blank C60 (60 minutes).uef` and `Blank C90 (90 minutes).uef`. Each is a UEF 0.10 holding an
&0000 origin chunk reading `Beebium` and one &0116 gap of its length (900, 1800, 3600 or
5400 s, exact in float32), with no date and no other chunk. They live in `tapes/blank/` and
`scripts/make_blank_tapes.py` writes them byte-reproducibly from a table; its `--check`, which a
ctest runs, fails unless the committed files and the set match what it makes.

`CassetteService.ListBlankTapes` lists them by label in file name order with their lengths,
and `CreateTape` copies one into place on the server's host as `DiscService.CreateDisc` does a
disc: a destination without an extension gets `.uef`, another extension is refused, and an
existing destination is refused unless `overwrite` is set. The CLI's `list-blank-tapes` and
`create-tape` do the same (see [cli.md](cli.md)). Creating a tape and inserting it are separate
steps, and the new tape is recordable because it is a UEF.

## CassetteService

`CassetteService` (`src/service/proto/cassette.proto`, implemented in
`service/CassetteService.hpp`) is registered in every server beside `DiscService`.

| RPC | Behaviour |
|---|---|
| `InsertTape(url, channel)` | Opens a UEF, CSW or WAV image the server can reach, given as a `file://` URI or an absolute path and judged by content, and leaves the deck stopped at the start. `channel`, `"left"` or `"right"`, chooses a stereo WAV's; without it the louder plays. Returns the tape's metadata. The file is read off the emulation thread; the insertion is made under pause |
| `EjectTape()` | Stops the deck and removes the tape |
| `Play()`, `Stop()` | The deck's keys. Play at the end of the tape succeeds and leaves the deck stopped |
| `Record()` | Press record; refused with `READ_ONLY` for a tape that cannot be recorded on (a CSW) |
| `Rewind()`, `FastForward()` | Wind at 30 times real speed, ungated by the relay, until stop, play, seek, eject or an end of the tape |
| `Seek(position_seconds)` | Moves the tape at once and ends a wind; a negative position is refused, and one beyond the end lands at the end |
| `GetDeckStatus()` | Presence, the tape and its metadata (name, URL, format, title, origin, target machine, duration to the millisecond, whether it is recordable), transport, position, relay, the ULA part, and whether the tape's signal map is ready and how many have been published |
| `GetTapeSignalMap()` | The tape's signal map, a level a second; see Signal map |
| `ListBlankTapes()`, `CreateTape(label, destination_path, overwrite)` | The blank tapes, and a copy of one into place; see Blank tapes |
| `SubscribeDeckStatus(min_interval_ms)` | The status at once, then on every change of presence, transport, relay or tape, and while the deck plays or records the position, at most every `min_interval_ms` of emulated time (default 100); while it winds, every thirty times that much tape |

Every unsuccessful request returns `success = false`, a sentence naming the tape by the name
the client gave, and a `CassetteErrorKind`:

| Kind | When |
|---|---|
| `NO_CASSETTE_INTERFACE` | The machine has no cassette interface |
| `CANNOT_OPEN` | The file could not be found, is empty, or could not be opened |
| `UNREADABLE` | The file could not be read |
| `UNSUPPORTED_FORMAT` | It is not a UEF, CSW or WAV tape image, or is a WAV of an encoding or channel count Beebium does not play |
| `INVALID_IMAGE` | It is a UEF or CSW whose contents break the format, or holds a disc, ROM or machine state |
| `DECK_OCCUPIED` | A tape is already in; the sentence names both tapes |
| `DECK_EMPTY` | Play, stop, record, rewind, fast forward, seek, eject or a signal map with no tape in |
| `INVALID_POSITION` | A seek before the start of the tape |
| `READ_ONLY` | Record on a tape Beebium only reads (a CSW, or a UEF with an &0113 change of base frequency) |

For example: "The deck already holds game.uef; eject it before inserting other.uef."

The field `DeckStatus.tape_inserted` says whether a tape is in. It is not called `has_tape`
because protobuf derives `has_tape()` from the `tape` field.

The clients mirror the service. The Python client provides `bbc.cassette` and
`Beebium.boot_tape`, and the TypeScript client provides `Beebium.cassette` and `bootTape`. Each
boot call inserts the tape, types `*TAPE` and a load command (`CHAIN""` unless told otherwise),
and presses play. Both clients also provide `record()`, `list_blank_tapes()` / `listBlankTapes()`
and `create_tape()` / `createTape()`; a failed create raises `CreateTapeError` with its kind.

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

The emulation thread, or a service thread with the emulation paused, only posts a job. Opening
the deck's own copy of a tape costs about 1.2 ms on the inserting thread. The latest job wins:
posting one, an eject, or a recording ending cancels a walk in progress, and a superseded walk
never publishes. A walk checks for cancellation once per item of its tape, and no item costs
more than a block of a WAV's samples: a `SampledTape` returns a `Gap` after 4096 samples with no
edge, rather than scanning a whole silence in one `next()`. That also bounds what one `next()`
costs the emulation thread when it plays a WAV. An eject during a walk of the longest corpus take
returns in under 0.1 ms. The mapper is the deck's last member, so it is destroyed first: its
destructor cancels the walk and joins the worker. No walk outlives the deck, and a job's opener
owns what it reads (a file path, or a shared copy of the bytes), never the deck's tape. The map is
not persisted; it is recomputed on each insert.

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
  `{"has_cassette": true, "label": "Cassette", "image_types": ["uef", "csw"]}`;
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

`--tape <filepath>` inserts a tape at launch and plays it, unlike a preset's tape. It is
anchored as `--floppy` is: a relative path is taken from the working directory, and a `file://`
URI is accepted. It wins over a preset's `storage.cassette`. Startup reports what was done, as
`Loading tape: <path> (playing)`, `(stopped)` or `(stopped at N s)`.

A file that is not a tape image ends the server with exit status CONFIG (78) and the reader's
sentence. A tape that cannot be found ends it with NOINPUT (66). See [cli.md](cli.md).

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
| `tests/test_mc6850.cpp` | Real division at /1, /16 and /64; start-bit detection and mid-bit sampling; master reset clearing the control register and holding TxD at mark mid-character; the /DCD latch and its clearing on a data read; the receiver held idle while /DCD is high |
| `tests/test_serial_ula.cpp` | The 16/13 MHz clock and its dividers; 64 RS423 clocks a bit at every rate; the read of &FE10; cassette mode's /DCD, /CTS and silence; the part |
| `tests/test_serial_ula_cassette.cpp` | The separator in ULA clocks: bursts, timeouts and the data rule. A tape byte arriving only at the matching ACIA divider. The detector: threshold for each part, re-arming by a 0, by motor off and by silence, and surviving latch writes. The VLSI inversion. The synthesiser's tones and silence; TDRE with no sink. The deck's movement, its refusal of a second tape, and Break. Winding: rewind from 60 s reaching 0 in 2 s and fast forward of a 90 s tape reaching its end in 3 s, with the relay open and no edges; stop and play mid-wind; a seek ending a wind |
| `tests/test_tape_recording.cpp` | The classifier, chunk by chunk (carrier and its split, a 900 s silence, `&0112`, an 8N1 byte, a 300-baud byte, `&0104`, 7E1 as 8N1, `&0102`, metadata first); round trips of a built tape, plain and gzipped, and of a recording with its stretch; recording silence over carrier; recording to the end; a recording through the deck read back to the clock; what ends a recording; write-back on stop, eject, flush and the deck's end, never for an unrecorded tape, and a failed write leaving the file whole; `record()` refused on a CSW and an empty deck; each blank tape's length, silence and recordability |
| `tests/test_tape_signal.cpp` | Every UEF chunk the reader renders, refuses or skips; gzip; metadata; seek and duration; CSW against the same tape's UEF to within one clock, both versions, polarity, long pulses, and the refusals. WAV: every sample format, the refusals, the louder channel and a chosen one, reading from the file and seeking, and a UEF rendered as 90° sines playing as the same half-cycle classes |
| `tests/test_front_end.cpp` | The model's constants; its phase at 1200 and 2400 Hz; 50 Hz and 8 kHz rejection; the comparator at 0.08 and 0.01, a full-scale square, noise alone and under a sine; interpolated edges 256 ± 2 clocks apart at 48 kHz; the same edges within 2 clocks at 22050, 44100, 48000 and 96000 Hz |
| `tests/test_cassette_mos.cpp` | With MOS 1.20 as a test vector: `*TAPE` and `CHAIN""` loading and running a BASIC program from UEF, at 300 baud after `*TAPE3`, and from CSW. `Data?` and `Rewind tape` for a bad data CRC; `Block?` for a block out of sequence. The Welcome tape's short leaders. `*CAT` with the tape running only while the relay is on; searching until the deck plays. `SAVE` to a copy of the C15 blank with the deck recording, at 1200 baud and at 300, `CHAIN`ed back in the same machine and a fresh one, and the written file's chunks. `tape_ok` as a WAV: 90° sines, sines at 0.18 with noise at 0.02, and square waves load; 0° sines do not (the model's prediction) |
| `tests/test_tape_signal_map.cpp` | The signal map: a builder UEF of gaps, carrier and blocks to its levels, including half-second boundaries; a 300 baud block as signal throughout; the same tape as a WAV within 16 a second, clean, with noise at 0.01 and with 50 Hz hum at 0.5 in its gaps, and as a CSW within 2; a superseded job never publishing; clearing or destroying the mapper stopping a ten-minute WAV walk within 100 ms; the deck mapping a CSW and a stereo WAV from their files and a UEF from its image, none for a file-less CSW or a live tape, and forgetting the map on eject |
| `tests/test_grpc_cassette.cpp` | `CassetteService`: status on a fresh machine; a stereo WAV's rate and its louder or chosen channel; insert, refusal of a second tape, and the error kinds; winding at 30 times real speed with the motor off; play gated by the relay; the status stream; a BASIC program loaded through MOS 1.20; `Record` on an empty deck, a CSW and a UEF, the file rewritten on `Stop`; the blank tapes listed, created, refused and recorded on. `StreamAudio`: its refusals; a live tape inserted, played, refusing what it cannot do, carrying its fill and counts; a stream that ends playing out its ring when the deck plays and going at once when it is stopped; `EjectTape` ending a stream; MOS 1.20 loading tape_ok streamed in real time to a machine at real speed, with and without the relay blipping during the leader. `GetTapeSignalMap`: `DECK_EMPTY` with no tape, a UEF's levels and seconds once ready, the generation counting, a live tape never ready, and the status stream reporting the map ready |
| `tests/test_blank_discs.cpp` | `list-blank-tapes` and `create-tape` over the committed templates, beside the disc subcommands |
| `tests/test_live_source.cpp` | The ring: a tone paced through it with no runs and its edges 256 clocks apart; priming a late stream, starving and priming again; an ended stream played out without priming; silence counted when a stream stops; a stream 10% fast dropping at the excess rate, the oldest; a producer and consumer on two threads; S16 and F32, mono and either stereo channel, in chunks that split frames, giving the same edges; the deck's rules for a live tape |
| `tests/test_grpc_indicator.cpp` | The motor LED listed on every model between the lock LEDs and the drives, following latch bit 7 |
| `tests/test_preset_subcommands.cpp` | `resolve-preset` and `describe-preset-schema` for the deck; a preset's tape inserted stopped; `--tape` winning and playing; a bare name found in `tapes/` |
| `clients/beebium-python-client/tests/test_cassette.py` | `bbc.cassette` round trips and winding, errors with kinds and sentences, the status stream, `boot_tape` running a tokenised BASIC program, blank tapes listed and created, and record as a transport with a CSW read-only; the README's recording snippet SAVEs to a new C15 and CHAINs it back |
| `clients/beebium-python-client/tests/test_cassette_live.py` | `stream_audio`'s summary; a live tape's refusals; the refusals of a stream; `stream_wav` of tape_ok in real time loading through MOS 1.20 |
| `clients/beebium-python-client/tests/test_saved_machines.py` | A saved machine resuming its tape where it was saved, and a run-time tape not recorded |
| `clients/beebium-typescript-client/tests/cassette.test.ts`, `cassette-integration.test.ts` | The TypeScript mirror, with mocked stubs and against a server |

`clients/beebium-python-client/tests/test_cassette_corpus.py` loads two collections from the
directory `BEEBIUM_TAPE_CORPUS_DIRPATH` names. First, commercial tapes as UEF or CSW, each
chosen because it exercises a particular part of the path:

- the /DCD latch;
- short carrier tones;
- the motor toggling during carrier;
- tone and data with no gap between them;
- the ACIA's master reset;
- writes to the ACIA during a load.

Each is identified by its SHA-256. Second, a corpus of recordings supplied for the project, WAV
files of real tapes played on a deck, named by file, each with a TOML beside it naming the signal
channel:

- Plan B2, side 1: takes 001, 002, level-normal and level-high reach the game's title screen with
  no `Data?` or `Block?`; the level-low take is a known failure (below), asserted as one;
- Cascade 50, side 1, take 1, whose TOML warns of bad patches (`Data?` in GAL and PLA): `CHAIN
  "LUN"` loads the program that lies past them;
- Plan B2, side 1, take 001, streamed live in real time through `stream_wav` (marked slow: it
  takes as long as the tape);
- Palace of Magic, side 1, the partial take with a play-stop-rewind-play in it: seeked past the
  rewind, found in the WAV as the longest stretch without tone, the loader loads from the replay
  and the game's blocks follow.

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
    115 cycles after sending the sync byte) could send the sync byte twice.
13. The signal map's run spacing of 4 ms (`SIGNAL_RUN_SPACING`): an edge further than that from
    the last ends a run, so a second is silent where no tone is.

Known simplification, not a Decision: a recording written back to a UEF can be a few clocks
longer than the tape in the deck. A recorded C15 comes back 11 clocks (9 µs) over 900 s,
because the writer's &0116 gap chunk holds its length as a 32-bit float. The deck reports the
duration to the millisecond, and the signal map counts its seconds from that, so neither shows
it.

## Not yet built

- Recording to CSW.
- Selecting the VLSI part from a preset.
- The GUI clients' live input (an input-device picker and the ring shown), built against chapter
  9.0 of the [client specification](client-specification.md), as their deck is.
