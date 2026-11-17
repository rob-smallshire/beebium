# The cassette interface: programme specification (#208, round 2)

Round 1 was the research survey, `cassette-research-findings.md` (the "findings"); its
section numbers are cited as F1.6, F7.1 and so on. This document is the design: what Beebium
builds for the cassette interface, how it is layered, what each piece must do, and the rounds
that build it. Every point the findings leave open is settled here as a `Decision`; the ones
that rest on secondary sources or inference rather than on a datasheet or a measurement are
marked `Decision (unverified)` and listed at the end.

## Requirements

- Load from UEF, the de facto format for already-decoded tapes.
- Load from a WAV file, in real time.
- Load from a live audio stream played into a **line-in** audio input. Microphone input is
  not a goal: the client captures from an audio input device the user chooses, and applies no
  voice processing (no gain control, noise suppression or echo cancellation) to it.
- The cassette motor LED, which the Model B and B+ have on the keyboard beside the lock LEDs,
  shows the relay state whether or not a deck is connected.
- The tape user interface shows a position, perhaps as a slider. No block catalogue and no
  waveform: those belong to a transcription or analysis tool, not to Beebium. A coarse
  silent-or-not map along the position bar is allowed (round 9): it shows where the tape is
  quiet, not what is on it.
- Recording (SAVE to tape) onto a tape with room for it, and a **new blank tape** facility like
  the blank-disc one (Rob, 2026-11-16): canned blank tapes of several lengths, a C15 at least,
  bundled as template images that are copied into place when the user asks, their file names
  being what the user is offered. Nothing before the recording round may foreclose it.
- As everywhere in Beebium: no dependence on a particular MOS (MOS 1.20 is a test vector
  only), tests built from images the tests generate, the server as the single source of truth,
  nothing on the per-cycle path takes a lock.

## Principles

1. **One signal path.** Every source, UEF, CSW, WAV or live, becomes the same thing before the
   Serial ULA sees it: the squared cassette signal, a sequence of edges in emulated time. The
   ULA model never knows where the edges came from. UEF loading therefore exercises the same
   demodulator as a real recording does.
2. **The ULA is modelled as the hardware it is**, from the parameter table in F7.1: an
   edge-driven data separator with a missing-edge timeout, a burst clock to the ACIA, a
   recovered data line, and a high-tone detector that pulses /DCD. Baud rate on receive is
   the ACIA's business, as on the real machine.
3. **The deck is the user's; the relay is the Beeb's.** Play, stop and position belong to the
   person; the motor relay belongs to the emulated machine. Tape moves only when both allow it.
4. **Fidelity where software can see it, simplicity where it cannot.** Analogue voltages,
   staircase levels and the exact edge-to-burst delay are not modelled; timing that a loader
   can observe is.
5. **The clients own host audio.** Capturing a line-in is host hardware, like a keyboard or a
   gamepad; the client does it and streams samples to the server, which never opens an audio
   device.

## Architecture

```
 UefTape ──┐                               ┌──────────────┐  edges   ┌────────────────────┐
 CswTape ──┼─► TapeSignal (edges) ─────────►│ CassetteDeck │─────────►│ SerialUla, cassette │
 WAV file ─┼─► SampledSource ─► FrontEnd ──►│ play/stop,   │          │ side: separator,    │ RxC/RxD/DCD
 live PCM ─┘   (per sample)    (edges)     │ position,    │          │ high-tone detector, │──────────► Mc6850
                                           │ relay gating │          │ tone synthesiser    │◄────────── TxD/RTS
                                           └──────────────┘          └────────────────────┘
                                                  ▲                            ▲
                                      CassetteService (gRPC)        &FE10 latch, 2 MHz tick
```

Layers, from the tape inwards:

- **Tape sources** produce a `TapeSignal`: a stream of edges, each the emulated time of a
  transition of the squared signal, plus explicit silence (no edges) and end of tape. UEF and
  CSW produce edges exactly from whole cycles. A `SampledSource` (a WAV file, or the live PCM
  stream) produces samples at its own rate; the `FrontEnd` turns them into edges.
- **`CassetteDeck`** owns one tape source at a time and the transport: play or stopped,
  position in tape time, and whether tape time advances this tick (play, and the relay closed
  for a file source). It hands the ULA the edges whose time has come.
- **`SerialUla`, cassette side**: the data separator and high-tone detector of F7.1, clocked
  with the ULA's own clock; the transmit tone synthesiser, which produces edges for a future
  recorder.
- **`Mc6850`** is clocked by the ULA (receive and transmit clocks) and divides by 1, 16 or 64.
- **`CassetteService`** is the gRPC face of the deck; the **motor LED** is an indicator.

Everything in the deck and the ULA runs on the emulation thread. Live PCM crosses from a gRPC
thread into a lock-free ring; the file sources read ahead on the emulation thread.

## The Serial ULA: clock model

The ULA runs from 16 MHz / 13 = 1.2308 MHz (F1.1). Beebium ticks the serial socket at 2 MHz.

`Decision`: the ULA keeps a fractional accumulator of ULA clocks per 2 MHz tick (as the Music
5000 resampler does), so every ULA interval below is an integer number of ULA clocks and the
rates come out 0.16% above nominal, as on the machine. Tests assert in ULA clocks.

### Receive and transmit clocks to the ACIA

`Decision`: the ACIA is clocked, not told a bit period. `SerialUla` delivers receive clocks
(RxC) and transmit clocks (TxC) to `Mc6850`, which counts them and divides by 1, 16 or 64 as
its control register says (F1.3). The ULA may deliver clocks in batches (`receive_clocks(n)`)
provided the ACIA's sampling points are unchanged.

- **RS423 selected**: RxC and TxC are the ULA's divider outputs for the selected rates, 64
  times the baud rate (19200 baud: 1 clock per ULA clock; 75 baud: 1 per 256). With the ACIA
  at /64 the byte rate is the selected baud rate, as today; at /16 it is four times it, as on
  the machine.
- **Cassette selected**: TxC is the transmit divider output (the MOS sets 300, so 19.23 kHz,
  and the ACIA's /16 or /64 gives 1200 or 300 baud). RxC comes from the data separator below;
  the receive rate field is ignored (F1.3, BeebWiki).
- The byte-oriented RS423 seam (`SerialDataSource`/`SerialDataSink`) is unchanged in shape.
  The ULA frames a received byte into RxD levels and holds each level for 64 RxC clocks
  regardless of the ACIA's divider, because the far end runs at the selected baud rate and
  knows nothing of the ACIA's setting. At /64 the ACIA sees the bits it sees today; at /16 or
  /1 the guest reads garbage, as on the machine. The existing serial suites are the regression
  net (round 3 left them at 4235 C++ cases overall, with `test_mc6850` at 24 cases and
  `test_serial_ula` at 22), with their set-up constant corrected from &15 (/16, wrongly called "the MOS serial config")
  to &16 (/64, what MOS 1.20 uses on RS423).

`Mc6850` changes: real division of the receive and transmit clocks, start-bit detection by
sampling (low for 8 or 32 clocks at /16 and /64, F1.3) and mid-bit sampling thereafter; the
`/DCD` latch below; master reset clears the control register (`Decision (unverified)`, beebjit's
measurement, Frak needs it, F3.4). Its header comment about b2 having removed clock division is
deleted; current b2 divides (F1.3).

### The &FE10 latch

Bits as today (F1.2). Two changes:

- `Decision (unverified)`: a read of &FE10 acts as a write of &FE, %11111110: motor on, RS423
  selected, receive 75, transmit 2400, so a stray read during a tape load leaves cassette mode
  (the ULA has no R/W pin and
  latches the bus, which on a read holds the address high byte). The read still returns open
  bus. Every reference emulator and the replica do this; Acorn does not document it.
- Bit 6 no longer drives /DCD directly. In RS423 mode /DCD is forced low (carrier present) as
  today; in cassette mode /DCD comes from the high-tone detector. /CTS is forced low in cassette
  mode (AUG §20.7.4).
- Bit 3 in cassette mode: on the VLSI part it inverts the tone sense on receive and transmit;
  the Ferranti ignores bits 3 to 5 (F7.1).

### The part

`Decision`: `SerialUla` is parameterised by part, `Ferranti` or `VLSI`, chosen at assembly. The
Model B family defaults to Ferranti; a VLSI part may be selected (an issue 7 Model B with one
has been seen, and titles that fail on it are the regression cases). The parameters that
differ: the DCD mechanism and threshold, the bit 3 tone swap, and transmit polarity. The
preset key and CLI option are settled in round 3's specification; the default needs neither.

## The Serial ULA: the data separator

From F7.1. All intervals in ULA clocks (812.5 ns); the table gives the values.

| Parameter | Value | Status |
|---|---|---|
| Missing-edge timeout | 350 clocks (284 µs) | `Decision (unverified)`: measured 260 and 284 µs on Ferranti parts; the replica uses 286 µs; a Ferranti needs about 300 µs for a reliable 0 |
| Burst | 4 RxC pulses of 2 clocks each (1.625 µs low, 1.625 µs high), starting at the edge | `Decision`: the real 20 to 25 µs edge-to-burst delay is not modelled (not observable by software) |
| Edge during a burst | the burst stops; the new edge starts a fresh one | `Decision`: as the real part, not the replica, which accepted signals real chips reject |
| Data rule | at each edge, from the gap just ended: long gap (timed out) gives 0; two consecutive short gaps give 1; otherwise hold | measured and in the replica |
| Polarity | edges of either sense are equal | measured |

Behaviour:

- Each edge starts a burst. If no edge arrives within the timeout, a second burst is emitted
  and the gap counts as long. A 2400 Hz half-cycle (256 clocks) never times out; a 1200 Hz
  half-cycle (512 clocks) always does, once.
- So a 1200-baud bit of either tone yields 16 RxC clocks, and 300 baud yields 64, which is why
  the ACIA divider alone sets the rate.
- Silence yields one timeout burst after the last edge and then nothing; RxD holds its value.
- There is no error correction: a dropout reads as low tone, a spike adds an edge.

## The Serial ULA: the high-tone detector and /DCD

From F1.6 and F7.1. `Decision`: the replica's counter rule, parameterised by part:

- A cell is 256 ULA clocks (208 µs). A counter advances by one for each cell in which the
  recovered data is 1 and high-tone edges are arriving, while the motor bit is 1.
- At the threshold the ULA raises /DCD for one cell and the counter stops until re-armed.
  Thresholds: Ferranti 962 cells (200 ms); VLSI 445 cells (92.7 ms).
- Re-armed (counter cleared, /DCD low) by: a recovered 0; the motor bit going to 0. Not by
  writes that keep the motor on, whether the value repeats or changes; not by switching from
  RS423 to cassette, during which the counter keeps advancing with the motor on.
- `Decision (unverified)`: silence with the motor on **holds** the count (the Ferranti's RC
  stops charging but is not discharged; the replica holds). Emulators clear it. Test cases:
  Uridium's tone-data-tone with no gap, Atic Atac's motor toggling, StarDrifter's short carrier,
  the B-Em-TOH DCD tests, when the corpus is to hand.
- `Decision`: the pulse is one cell for both parts; the Ferranti's edge alignment and its
  frequency acceptance threshold (about 1790 Hz) are not modelled. The chip-to-chip spread of
  the Ferranti threshold (186 to 215 ms) is represented by the single value 200 ms.

The ACIA side (F1.6, 6850 datasheet against measurement): `Decision (unverified)`: a low-to-high
transition on /DCD sets status bit 2, raises the IRQ if CR7 is set, and the bit stays set
until the **data register** is read (beebjit and jsbeeb's measurement; the datasheet's
status-then-data sequence is not followed). While /DCD is high the receiver is held in reset
and RDRF reads clear (datasheet); with a one-cell pulse this is invisible in practice and is
implemented because it is the documented behaviour.

MOS 1.20's state machine (F2.2) is the first test vector for all of this: DCD during leader,
clear by the time &2A arrives, `Data?` on a DCD during data.

## The Serial ULA: transmit

From F1.7 and F7.1. `Decision`: the transmit side is built in the reading rounds because the
ULA always runs it and loaders observe TDRE:

- The tone synthesiser runs from the master clock: 2403.8 Hz for mark, 1201.9 Hz for space,
  following TxD, phase-continuous, tone changes at zero crossings, TxD sampled at 832 µs cell
  boundaries. RTS high, or RS423 selected, gives silence.
- Its output is a `TapeSignal` of edges, the same type the deck consumes, so a recorder (a
  later round) is a sink of edges and can write CSW exactly or UEF by decoding its own edges.
- With no recorder, edges are discarded. Bytes clock out at the real rate; TDRE returns when the
  ACIA says so (Pro Boxing Simulator depends on it, F3.4).
- The staircase levels and the VLSI's opposite polarity are recorded as part parameters for
  the recording round and not otherwise modelled.

## Tape sources

### `TapeSignal`

The seam between a tape and the deck. A source yields, in order, items of three kinds: an
**edge** at a tape time; a **gap** to a tape time (no edges until then); **end**. Tape time is
in ULA clocks from the start of the tape, 64-bit. A source is rewindable and seekable to a tape
time, and reports its total duration when it knows it (files) or not (live). The deck pulls
items ahead of the current tape time; nothing in a source is called from anywhere but the
emulation thread.

### UEF

`Decision` on the chunk set (F4.1). A reader, `UefTape`, that:

- accepts the `UEF File!` signature, any minor version, major version 0, plain or gzipped
  (detected by the `1F 8B` magic);
- renders each chunk into edges from whole cycles at the current base frequency (default
  1200 Hz; carrier at twice it), as ideal square cycles: a cycle is a low half then a high half
  of equal length, at the phase &0115 set (default 180°). `Decision`: UEF is rendered as ideal
  squares and not through the front-end model, so UEF loading is phase-insensitive; sampled
  sources inherit the real machine's phase sensitivity through the front end (F7.1, Q27);
- handles &0100 (8N1); &0104 exactly, including parity and negative stop-bit counts, and the
  MakeUEF-before-2.4 parity swap keyed on the &0000 origin string (F3.4); &0110; &0111 (cycles,
  &AA as 8N1, cycles); &0112 as n/(2 × base) seconds; &0113 as a change of base frequency
  (`Decision`: the canonical draft 28 meaning; draft 25's "baud" meaning is not supported);
  &0114 security cycles with `Decision (unverified)`: least significant bit first and 1 = long
  cycle, the order the specification's own worked examples decode under; &0115; &0116 as
  seconds; &0117 selecting the 1-or-2 cycle (1200) or 4-or-8 cycle (300) bit encoding;
- keeps &0000, &0001, &0005, &0009, &0120, &0130 and &0131 as metadata the service can report
  (title, origin, target machine) and ignores &0003, &0006 to &0008, &000A, &0101, &0103 and
  &0105;
- refuses a file containing disc, ROM or machine-state chunks (&0200 and above) with a sentence
  naming the chunk; logs and skips any other unknown chunk.

### CSW

`Decision` (F4.2): `CswTape` reads v1.01 and v2, RLE and Z-RLE, any sample rate, honours the
initial polarity flag and the &00 long-pulse escape, and converts each pulse length to an edge
at pulse_length / sample_rate in ULA clocks with an error-accumulating rounder. A pulse longer
than 100 ms is treated as a gap. v1 files with a sample rate of 0 are refused with a sentence.

### Sampled sources and the front end

A `SampledSource` gives mono samples at a rate. Two exist:

- **WAV file**: PCM 8, 16, 24 or 32-bit integer and 32-bit float, any rate, mono or stereo.
  `Decision`: for stereo the channel with the higher RMS over the first 10 seconds is used
  unless the user chooses one (the corpus records on the left; most archive WAVs are mono).
- **Live PCM**: the stream `CassetteService.StreamAudio` delivers (below), through a lock-free
  single-producer single-consumer ring sized for 2 seconds.

The `FrontEnd` runs per sample on the emulation thread, in emulated time: a sample is consumed
each time the fractional accumulator crosses the sample period (41.67 2 MHz ticks at 48 kHz).
`Decision` on its model (F7.1 front-end table, F5.6):

1. DC blocking: a one-pole high-pass at 20 Hz.
2. A second-order Sallen-Key high-pass at 1.29 kHz and a second-order low-pass at 4.13 kHz, as
   biquads at the source's rate, matching the LM324 stages so the low tone's crossings move as
   they do on the machine (about +65° at 1200 Hz).
3. A comparator with hysteresis: the output flips when the filtered signal passes ±h, with h
   set at 2% of full scale. The real clipper has a gain of about 50 and the ULA input a fixed
   threshold, so almost any signal above a few millivolts squares cleanly; 2% keeps the corpus's
   low-level take (peak 18%, RMS 5%) decoding and rejects line noise.
4. Each comparator transition is an edge at the current emulated time.

The constants are parameters of the model, asserted by tests, and marked `Decision (unverified)`
as a group: they are to be checked against the corpus (all three levels must load) and the
DCD-test WAVs. No normalisation and no automatic gain.

Cost: about 20 to 50 host cycles per sample, 0.1% to 0.3% of the emulation thread's budget on a
Pi (F7.5). The pacing statistics show it if that estimate is wrong.

## The deck

`CassetteDeck`, owned by the hardware policy beside the disc drives, present on every variant
Beebium builds (F6). Presence is a hardware-policy trait, `has_cassette`, so a future Master
Compact can say no while still having the ACIA.

State: the tape (none, or one source with its name and kind: UEF, CSW, WAV, live); transport
`stopped`, `playing`, `recording` (round 6), `rewinding` or `fast_forwarding`; position in tape time; total duration if known; the relay (from the
ULA); and the metadata the source offers.

Rules:

- **Movement.** Tape time advances on a tick when the transport is `playing` and, for a file
  source, the relay is closed. `Decision`: the motor stops the tape instantly, with no
  inertia; the MOS's 1 ms header blip therefore stops the tape for 1 ms. To revisit on Atic Atac.
- **Live source.** `Decision`: the relay does not gate a live source. A deck without a remote
  lead keeps playing and the real ULA keeps listening to it; the relay still re-arms the
  high-tone detector, which is what the MOS's blip is for. Samples that arrive while the
  emulator is paused, or faster than emulated time consumes them, fill the ring; when it is
  full the oldest are dropped, the guest sees a discontinuity, and the filing system reports it
  as it would a dropout. Below real speed the ring runs dry and the guest hears silence. Neither
  is prevented; the status reports the ring's fill so a client can show it.
- **Play and stop** are the user's. Inserting a tape leaves the deck stopped; a client may
  offer insert-and-play as one gesture (two calls). **Seek** is instant to a tape time. End of
  tape stops the transport (`Decision`: no looping).
- **Winding** (Rob, 2026-11-16: "a back to the beginning button as well as rewind"). `Rewind`
  and `FastForward` are transport states in which tape time runs backwards or forwards at a
  fixed wind rate (`Decision`: 30 times real speed, a C60 side-to-side in two minutes, as a
  real deck), not gated by the relay, ended by `Stop`, `Play`, `Seek`, eject or either end of
  the tape, where the deck stops. The ULA receives no edges while winding. Back-to-beginning is
  `Seek(0)`, instant.
- **Eject** stops and removes the tape. A live source is "ejected" when its stream ends.
- **Position** is reported in seconds (tape time / ULA clock rate) with the duration, so a
  client can draw a slider. No catalogue, no block names, no waveform.
- **Reset.** Break and power cycle do not touch the deck: a tape in a deck stays where it is.
  The relay opens on reset because the latch resets (F1.2).

## Service

`CassetteService`, `src/service/proto/cassette.proto`, on the DiscService pattern. All sentences
name the tape by the name the client gave.

| RPC | Purpose |
|---|---|
| `InsertTape(url)` | Open a UEF, CSW or WAV the server can reach; the server judges the format by content. Returns success, or a human sentence and a kind: `NOT_FOUND`, `UNREADABLE`, `UNSUPPORTED_FORMAT`, `DECK_OCCUPIED`, `NO_CASSETTE_INTERFACE` |
| `EjectTape()` | Stop and remove |
| `Play()`, `Stop()`, `Seek(seconds)` | The deck's keys |
| `GetDeckStatus()` | Presence, tape name and kind, transport, position, duration, relay, metadata (title, origin, target machine), live ring fill |
| `SubscribeDeckStatus(min_interval_ms)` | A stream of the same, on change |
| `StreamAudio(stream AudioInputChunk)` | Client-streaming: the first message carries the sample rate and format (16-bit or float32 mono PCM); the rest carry samples. The stream is the live tape; it is refused with `DECK_OCCUPIED` if a tape is in. Closing the stream ejects |
| `ListBlankTapes()`, `CreateTape(label, destination_path, overwrite)` | The mirror of `ListBlankDiscs`/`CreateDisc`: the bundled blank-tape templates by label, and a copy of one into place on the server's host, with the same error kinds (unknown label, destination exists, write failed, wrong extension). Recording round |

The fingerprint changes; the next release after this lands is a minor.

Errors follow `feedback_user_facing_error_text_is_a_sentence`: "The file game.uef is not a UEF,
CSW or WAV tape image." and so on.

## Blank tapes and recording

A blank tape is a bundled template, as a blank disc is: `tapes/blank/<label>.uef`, committed,
byte-reproducible from `scripts/make_blank_tapes.py` (a UEF whose tape content is one gap of
the tape's length, with an origin chunk naming Beebium and no date), copied into the build
tree and installed to `share/beebium/blank-tapes` beside `blank-discs`. The label is the file
name without its extension and is what the user is offered. `Decision`: the lengths are the
compact cassette's, C15, C30, C60 and C90, labelled "Blank C15 (15 minutes)" and so on, and the
whole nominal length is one Beebium tape: the deck has no side to turn over, and a side-flip
gesture would be a GUI addition for no gain. A blank tape named by a preset is copied into the
machine's directory like any preset media, so what the Beeb records lands in the machine's own
copy.

Recording is the transmit synthesiser's edges, which the deck already receives as a
`TapeSignal` of the same kind it plays, written onto the tape in the deck from the current
position while the deck is **recording** (a third transport state the user selects, like a
real deck's record key; the relay gates it as it gates play) and the relay is closed. The
recorded span replaces what the tape held there; the tape's length is fixed, and recording
stops at the end. The image is rewritten on eject, on `SaveMachine` and at shutdown, as a UEF
in the chunks that describe what was recorded without decoding it through any MOS's framing:
carrier as &0110 cycles, silence as &0116, and data as &0114 raw cycles unless clean 8N1 at
the cell rate is found, when it becomes &0100 so that other tools read it; the recording
specification settles the detail. A CSW or WAV in the deck is read-only (`READ_ONLY` kind) until
a later round says otherwise. `GetDeckStatus` reports whether the tape is recordable.

## The motor LED

The cassette motor LED is a real LED on the Model B and B+ keyboard, driven with the relay from
latch bit 7 (F1.8). `Decision`: an indicator `cassette-motor-led`, label "CASSETTE MOTOR",
colour red, registered by the hardware policy where the lock LEDs are, following bit 7 directly
whether or not a tape is in. The 1 ms blips are real and visible on a real machine as a flicker;
the client's indicator rendering applies whatever hold-off it applies to the drive LEDs.

## Presets and machine directories

- Preset schema: `storage.cassette: { "image_uri": path or URI or null }`. A tape in a preset is media the
  machine's directory copies, as a floppy named by search or position is; one named by an
  absolute path is opened in place. A live source is not persisted.
- As built in round 5: which tape is configuration, in the machine's `preset.json`
  (`storage.cassette`); its position is state, `"cassette": {"position_seconds": ...}` in
  `machine.json`, a field of the machine record carried through every rewrite. `SaveMachine`
  writes it only while the deck holds the configured tape and omits it otherwise; it is never
  written at shutdown, and `start --machine` puts the tape back there, stopped. A tape inserted at
  run time is not recorded, as a floppy inserted at run time is not.
- **The New Machine configurator** (both GUI clients, and the machine draft behind it) gains a
  cassette row in its Storage section, above the drives, so a machine or a preset can start with
  a tape inserted: browse and drag-and-drop for a UEF, CSW or WAV, written as `storage.cassette`
  and validated through `resolve-preset` like a floppy; `describe-preset-schema` reports whether
  the model has the interface (`has_cassette`). A bundled preset may carry its tape beside it as
  it carries a disc. The deck starts stopped: the user presses play, or the client's
  insert-and-play gesture does.
- The ULA part, if not the default, is a `hardware` key settled in round 3.

## Clients

- **Client specification**: a new chapter "Cassette" in the storage profile, written in round 5
  before any GUI client implements it: insert (browse, drag and drop, recent tapes through the
  recent-discs machinery, all server-judged), the deck keys (Rob's concessions, 2026-11-16: a
  back-to-the-beginning button beside Rewind and Fast forward, and a position slider the user
  scrubs with, guided only by a tape counter showing the position, "just like the real
  thing"; and no tape audio while playing, winding or scrubbing), a position slider from position
  and duration, the live input device picker (audio input devices the host lists, with no voice
  processing), and the error sentences. The chapter states that a client shows no catalogue or
  waveform.
- **Python**: `bbc.cassette.insert(path)`, `.eject()`, `.play()`, `.stop()`, `.seek(seconds)`,
  `.status()`, `.events()`, `.stream_audio(iterable, sample_rate)`, and `Beebium.boot_tape`
  in the shape of `boot_disc` (`*TAPE`, `CHAIN""`, play, wait in emulated time). A README
  section with a tested snippet.
- **TypeScript**: the mirror.
- **Placement in the storage sidebar**: the cassette section sits **above** the floppy drives,
  which sit above hard discs: the order of storage sophistication and performance, and the
  cassette is the one interface every BBC Micro has (the Master Compact apart, which Beebium
  does not yet build). The client-specification chapter states the order.
- **macOS and Linux apps**: later rounds against the chapter. Capture uses the audio input
  device the user picks; the macOS app needs the microphone usage string and the audio-input
  entitlement because the operating system gates every input, line-in included, behind that
  permission (F7.6).

## Tests

Test-first in each round. Tapes are built at test time: an in-repo writer (`tests/support/`
for C++, a helper in the Python tests) that takes bytes from `uvx oaknut-basic tokenise` or
beebasm and writes UEF (&0000, &0110 leader of 12240 cycles, per block one &0100 of &2A, header,
header CRC, data, data CRC, then &0110 of 2160 cycles, F2.4) and CSW (48 kHz, 10 and 20 sample
half-cycles, Z-RLE). CRC-16/XMODEM, check value &31C3 for "123456789".

Commercial tapes (beebjit's 18, the B-Em-TOH suite) and the WAV corpus are never committed.
`Decision`: tests that need them read `BEEBIUM_TAPE_CORPUS_DIRPATH`, identify tapes by SHA-256,
and `SKIP` with a sentence when the directory or the file is absent, as the Econet fixtures do.
The WAV corpus is read in place, read-only.

Catch2 names 7-bit ASCII, never starting with `--`; tags `[cassette]`, `[serial_ula]`,
`[mc6850]`, `[uef]`, `[csw]`, `[front_end]`.

## Rounds

Each round has its own short specification written from this document, is implemented by the
cassette agent, and ends with the full gate from a separate Release worktree.

1. **Round 3: the clock model.** *Done.* `Mc6850` divides for real and gains the /DCD latch and the
   master-reset rule; `SerialUla` delivers clocks, keeps RS423 byte framing, gains the part
   parameter and the &FE10 read rule. No cassette yet: in cassette mode /DCD is low and RxD
   idle. Gate: every serial test green; new tests for division, the latch, the reset rule, the
   read rule, and RS423 at /16.
2. **Round 4: the tape path.** *Done.* `TapeSignal`, `UefTape`, `CswTape`, `CassetteDeck`, the data
   separator, the high-tone detector, the transmit synthesiser with a discarding sink. CLI
   `--tape <file>` for tests. Tests: separator and detector in ULA clocks against the table;
   a generated UEF loads and runs through MOS 1.20 (`*TAPE`, `CHAIN""`, screen text), at 1200 and
   300 baud (&0117); the CSW of the same tape gives the same memory; `Data?` on a corrupted CRC;
   the MOS blip re-arms DCD; motor off stops the tape; a tape with a 0.25 s leader (the Welcome
   tape's shape) loads; beebjit's corpus cases where present.
3. **Round 5: service, indicator, persistence, clients.** *Done.* `CassetteService` without
   `StreamAudio`; the motor LED; preset and machine directory; Python and TypeScript; the
   client-specification chapter; `docs/cassette.md` and the updates to `serial-acia.md`
   (including its &15 claim, F-Q26), `grpc-server.md`, `preset-schema.md`, `indicators.md`,
   `machine-directory.md`, `client-specification.md`, CLAUDE.md, README.
4. **Round 6: recording and blank tapes.** *Done.* The recording transport state, the UEF writer, the
   templates and `scripts/make_blank_tapes.py`, `ListBlankTapes`/`CreateTape`, CLI
   `list-blank-tapes`/`create-tape`, Python and TypeScript mirrors, the client-specification
   section on creating blank tapes (the mirror of 9.3, gated on a shared filesystem). Tests: MOS
   1.20 SAVEs a program to a blank C15 and `CHAIN`s it back after rewind, in Beebium and then
   from the written UEF in a fresh machine; the written UEF's chunks are what the spec says; a
   recording over the middle of a tape leaves both ends; recording stops at the end; a
   recording through round 3's /DCD and the 5.1 s leader and 0.9 s gaps MOS writes are present
   as carrier. Brought forward so the GUI rounds build against the whole deck.
5. **Round 7: sampled sources.** *Done.* WAV, the front end, the corpus tests (every take of Plan B2
   at all three levels loads its first file; a Cascade 50 take loads a named program; the
   partial-rewind take loads after a seek), the DCD-test WAVs.
6. **Round 8: live audio.** *Done.* `StreamAudio`, the ring, the timing policy, Python
   `stream_audio` fed from a WAV (which makes the WAV corpus the live test vector too).
7. **Round 9: signal map.** *Server done; the GUI round follows.* One level per second of tape,
   silent or not, computed from the `TapeSignal` the deck would play on a worker the deck owns
   (`cassette-round9-signal-map-specification.md`), `GetTapeSignalMap` and the status fields,
   Python `signal_map()` and TypeScript `signalMap()`; the GUI clients then colour the position
   bar grey and blue by it.
8. **Later**: recording to CSW; the VLSI part's selection in presets; a Master Compact's
   `has_cassette = false` when that variant exists. The GUI clients' live-input round is done.

## Decisions to verify on hardware or against the corpus

1. Missing-edge timeout 284 µs (measure with the DCD-test WAVs and a signal generator).
2. The detector holding its count through silence with the motor on.
3. The one-cell DCD pulse for the Ferranti, in place of an edge-aligned half-cycle.
4. The ACIA /DCD latch clearing on a data read alone, and master reset clearing the control
   register.
5. A read of &FE10 acting as a write of &FE.
6. The &0114 bit order.
7. The front-end constants: 1.29 kHz, 4.13 kHz, 2% hysteresis, 20 Hz DC block.
8. The instantaneous motor (no inertia) against Atic Atac.
