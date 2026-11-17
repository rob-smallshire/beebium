# The cassette interface

The BBC Micro's cassette interface as Beebium builds it. It covers:

- the Serial ULA's cassette side: the data separator, the high-tone detector and the tone
  synthesiser, all clocked at 16/13 MHz;
- the MC6850 ACIA that divides the separator's clocks and latches /DCD;
- the tape sources: UEF and CSW images;
- the deck that plays them, and records on a UEF;
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
- `cassette-round6-recording-specification.md`: recording and blank tapes.

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
| UEF (plain or gzipped) and CSW (v1.01 and v2, RLE and Z-RLE) tapes, rendered as edges in ULA clocks | WAV files and live audio (later rounds, below) |
| One deck per machine: insert, eject, play, stop, rewind, fast forward, seek; the tape moving only while it plays with the relay closed, or while it winds | Motor inertia: the relay starts and stops the tape instantly. Tape audio: the tape reaches only the Serial ULA, and no client plays it |
| The motor relay as an indicator | |
| Recording on a UEF, written back to its file; four blank tapes | Recording on a CSW |

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
| `InsertTape(url)` | Opens a UEF or CSW image the server can reach, given as a `file://` URI or an absolute path and judged by content, and leaves the deck stopped at the start. Returns the tape's metadata. The file is read off the emulation thread; the insertion is made under pause |
| `EjectTape()` | Stops the deck and removes the tape |
| `Play()`, `Stop()` | The deck's keys. Play at the end of the tape succeeds and leaves the deck stopped |
| `Record()` | Press record; refused with `READ_ONLY` for a tape that cannot be recorded on (a CSW) |
| `Rewind()`, `FastForward()` | Wind at 30 times real speed, ungated by the relay, until stop, play, seek, eject or an end of the tape |
| `Seek(position_seconds)` | Moves the tape at once and ends a wind; a negative position is refused, and one beyond the end lands at the end |
| `GetDeckStatus()` | Presence, the tape and its metadata (name, URL, format, title, origin, target machine, duration to the millisecond, whether it is recordable), transport, position, relay and the ULA part |
| `ListBlankTapes()`, `CreateTape(label, destination_path, overwrite)` | The blank tapes, and a copy of one into place; see Blank tapes |
| `SubscribeDeckStatus(min_interval_ms)` | The status at once, then on every change of presence, transport, relay or tape, and while the deck plays or records the position, at most every `min_interval_ms` of emulated time (default 100); while it winds, every thirty times that much tape |

Every unsuccessful request returns `success = false`, a sentence naming the tape by the name
the client gave, and a `CassetteErrorKind`:

| Kind | When |
|---|---|
| `NO_CASSETTE_INTERFACE` | The machine has no cassette interface |
| `CANNOT_OPEN` | The file could not be found, is empty, or could not be opened |
| `UNREADABLE` | The file could not be read |
| `UNSUPPORTED_FORMAT` | It is not a UEF or CSW tape image |
| `INVALID_IMAGE` | It is a UEF or CSW whose contents break the format, or holds a disc, ROM or machine state |
| `DECK_OCCUPIED` | A tape is already in; the sentence names both tapes |
| `DECK_EMPTY` | Play, stop, record, rewind, fast forward, seek or eject with no tape in |
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

## The motor LED

`attach_cassette_motor_led` (`cassette/CassetteMotorLed.hpp`) registers the indicator
`cassette-motor-led` in every hardware policy, after the lock LEDs. Its label is "Cassette
motor", its color 625 nm (red) and its shape domed. It reads 255 while latch bit 7 is set and
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
- In Python, `clients/beebium-python-client/tests/tape_builder.py` does the same, independently
  of the C++ build.
- BASIC programs are tokenised from `.bas` sources in `tests/assets/cassette/`.

| Suite | What it covers |
|---|---|
| `tests/test_mc6850.cpp` | Real division at /1, /16 and /64; start-bit detection and mid-bit sampling; master reset clearing the control register and holding TxD at mark mid-character; the /DCD latch and its clearing on a data read; the receiver held idle while /DCD is high |
| `tests/test_serial_ula.cpp` | The 16/13 MHz clock and its dividers; 64 RS423 clocks a bit at every rate; the read of &FE10; cassette mode's /DCD, /CTS and silence; the part |
| `tests/test_serial_ula_cassette.cpp` | The separator in ULA clocks: bursts, timeouts and the data rule. A tape byte arriving only at the matching ACIA divider. The detector: threshold for each part, re-arming by a 0, by motor off and by silence, and surviving latch writes. The VLSI inversion. The synthesiser's tones and silence; TDRE with no sink. The deck's movement, its refusal of a second tape, and Break. Winding: rewind from 60 s reaching 0 in 2 s and fast forward of a 90 s tape reaching its end in 3 s, with the relay open and no edges; stop and play mid-wind; a seek ending a wind |
| `tests/test_tape_recording.cpp` | The classifier, chunk by chunk (carrier and its split, a 900 s silence, `&0112`, an 8N1 byte, a 300-baud byte, `&0104`, 7E1 as 8N1, `&0102`, metadata first); round trips of a built tape, plain and gzipped, and of a recording with its stretch; recording silence over carrier; recording to the end; a recording through the deck read back to the clock; what ends a recording; write-back on stop, eject, flush and the deck's end, never for an unrecorded tape, and a failed write leaving the file whole; `record()` refused on a CSW and an empty deck; each blank tape's length, silence and recordability |
| `tests/test_tape_signal.cpp` | Every UEF chunk the reader renders, refuses or skips; gzip; metadata; seek and duration; CSW against the same tape's UEF to within one clock, both versions, polarity, long pulses, and the refusals |
| `tests/test_cassette_mos.cpp` | With MOS 1.20 as a test vector: `*TAPE` and `CHAIN""` loading and running a BASIC program from UEF, at 300 baud after `*TAPE3`, and from CSW. `Data?` and `Rewind tape` for a bad data CRC; `Block?` for a block out of sequence. The Welcome tape's short leaders. `*CAT` with the tape running only while the relay is on; searching until the deck plays. `SAVE` to a copy of the C15 blank with the deck recording, at 1200 baud and at 300, `CHAIN`ed back in the same machine and a fresh one, and the written file's chunks |
| `tests/test_grpc_cassette.cpp` | `CassetteService`: status on a fresh machine; insert, refusal of a second tape, and the error kinds; winding at 30 times real speed with the motor off; play gated by the relay; the status stream; a BASIC program loaded through MOS 1.20; `Record` on an empty deck, a CSW and a UEF, the file rewritten on `Stop`; the blank tapes listed, created, refused and recorded on |
| `tests/test_blank_discs.cpp` | `list-blank-tapes` and `create-tape` over the committed templates, beside the disc subcommands |
| `tests/test_grpc_indicator.cpp` | The motor LED registered on every model after the lock LEDs, following latch bit 7 |
| `tests/test_preset_subcommands.cpp` | `resolve-preset` and `describe-preset-schema` for the deck; a preset's tape inserted stopped; `--tape` winning and playing; a bare name found in `tapes/` |
| `clients/beebium-python-client/tests/test_cassette.py` | `bbc.cassette` round trips and winding, errors with kinds and sentences, the status stream, `boot_tape` running a tokenised BASIC program, blank tapes listed and created, and record as a transport with a CSW read-only; the README's recording snippet SAVEs to a new C15 and CHAINs it back |
| `clients/beebium-python-client/tests/test_saved_machines.py` | A saved machine resuming its tape where it was saved, and a run-time tape not recorded |
| `clients/beebium-typescript-client/tests/cassette.test.ts`, `cassette-integration.test.ts` | The TypeScript mirror, with mocked stubs and against a server |

`clients/beebium-python-client/tests/test_cassette_corpus.py` loads commercial tapes, each
chosen because it exercises a particular part of the path:

- the /DCD latch;
- short carrier tones;
- the motor toggling during carrier;
- tone and data with no gap between them;
- the ACIA's master reset;
- writes to the ACIA during a load.

Each tape is identified by its SHA-256 and read from the directory that
`BEEBIUM_TAPE_CORPUS_DIRPATH` names. No commercial tape is committed, and a test skips when its
tape is absent.

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

## Not yet built

- **Round 7:** WAV files, through a model of the machine's analogue front end.
- **Round 8:** live audio, streamed to the deck by a client from a host input device.
- **Later:** recording to CSW, selecting the VLSI part from a preset, and the GUI clients' deck,
  built against chapter 9.0 of the [client specification](client-specification.md).
