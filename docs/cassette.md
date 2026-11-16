# The cassette interface

The BBC Micro's cassette interface as Beebium builds it. It covers:

- the Serial ULA's cassette side: the data separator, the high-tone detector and the tone
  synthesiser, all clocked at 16/13 MHz;
- the MC6850 ACIA that divides the separator's clocks and latches /DCD;
- the tape sources: UEF and CSW images;
- the deck that plays them;
- `CassetteService`, the motor LED, presets and machine directories, and `--tape`.

The tracking issue is #208.

The programme specification is `docs/discussion/cassette-specification.md`. It holds every
decision and the reason for it, citing the round 1 survey in
`docs/discussion/cassette-research-findings.md` (cited below as "findings"). Each round has
its own specification beside it:

- `cassette-round3-*`: the clock model;
- `cassette-round4-*`: the tape path;
- `cassette-round5-service-specification.md`: the service, LED, persistence and clients.

The serial side of the same chips, RS423, is in [serial-acia.md](serial-acia.md).

## What is modelled and what is not

| Modelled | Not modelled (yet) |
|---|---|
| The ULA clocked at 16 MHz / 13 from a fractional accumulator on the 2 MHz tick, so every interval is a whole number of ULA clocks and the rates run 0.16% above nominal, as on the machine | The real part's 20 to 25 µs delay from an edge to its burst of receive clocks (not observable by software) |
| The data separator: a burst of four receive clocks at each edge, a 350-clock missing-edge timeout, the data rule from the gap just ended | Error correction: there is none on the machine either; a dropout reads as low tone |
| The high-tone detector: a counter of 256-clock cells, a one-cell /DCD pulse at 962 cells (Ferranti) or 445 (VLSI), and the rules that re-arm it | The Ferranti's edge-aligned pulse, its frequency acceptance threshold (about 1790 Hz), and the chip-to-chip spread of its 200 ms |
| The tone synthesiser: 2403.8 Hz for mark, 1201.9 Hz for space, phase-continuous, sampling TxD at each 1024-clock cell boundary | The staircase output levels and the VLSI part's opposite polarity (recorded for the recording round) |
| The ACIA dividing real receive and transmit clocks by 1, 16 or 64, and the /DCD latch | |
| The Ferranti part (the default) and the VLSI part, including the VLSI's tone inversion on latch bit 3 | Selecting the VLSI part from a preset |
| UEF (plain or gzipped) and CSW (v1.01 and v2, RLE and Z-RLE) tapes, rendered as edges in ULA clocks | WAV files and live audio (later rounds, below) |
| One deck per machine: insert, eject, play, stop, seek; the tape moving only while it plays with the relay closed | Motor inertia: the relay starts and stops the tape instantly |
| The motor relay as an indicator | Recording, and blank tapes (round 6) |

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
1201.9 Hz for a 0 (space), phase-continuous, as edges to an optional `TapeSink`. RTS high or
RS423 selected gives silence. With no sink attached the edges are discarded. Bytes still clock
out at the real rate, and TDRE returns when the ACIA says so, which some loaders observe.

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

The reader's chunk table is laid out so that a UEF writer (round 6) can mirror it.

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
- the transport: stopped or playing;
- the position, in tape time;
- the relay, from the ULA.

The deck's rules:

- **Movement.** Tape time advances while the transport is playing and the relay is closed. The
  relay stops the tape instantly, so the MOS's 1 ms blip stops it for 1 ms.
- **Insert** leaves the deck stopped at the start. A second tape is refused while one is in:
  removing a tape is a deliberate eject.
- **Play and stop** are the user's. A client may offer insert-and-play as one gesture, made as
  two requests.
- **Seek** moves to a tape time at once, and rewind is a seek to zero. A seek beyond the end
  lands at the end and stops the deck. At the end of the tape the transport stops; the tape does
  not loop.
- **Eject** stops the deck and removes the tape.
- **Reset.** Break and power cycle leave the deck alone. The relay opens because the latch
  resets.

The deck publishes its state through atomics: presence, transport, relay, position and a tape
generation. While the deck plays, the position is published at most every 4096 ULA clocks.
The service reads these without stopping the machine.

## CassetteService

`CassetteService` (`src/service/proto/cassette.proto`, implemented in
`service/CassetteService.hpp`) is registered in every server beside `DiscService`.

| RPC | Behaviour |
|---|---|
| `InsertTape(url)` | Opens a UEF or CSW image the server can reach, given as a `file://` URI or an absolute path and judged by content, and leaves the deck stopped at the start. Returns the tape's metadata. The file is read off the emulation thread; the insertion is made under pause |
| `EjectTape()` | Stops the deck and removes the tape |
| `Play()`, `Stop()` | The deck's keys. Play at the end of the tape succeeds and leaves the deck stopped |
| `Seek(position_seconds)` | Moves the tape; a negative position is refused, and one beyond the end lands at the end |
| `GetDeckStatus()` | Presence, the tape and its metadata (name, URL, format, title, origin, target machine, duration), transport, position, relay and the ULA part |
| `SubscribeDeckStatus(min_interval_ms)` | The status at once, then on every change of presence, transport, relay or tape, and while the deck plays the position, at most every `min_interval_ms` of emulated time (default 100) |

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
| `DECK_EMPTY` | Play, stop, seek or eject with no tape in |
| `INVALID_POSITION` | A seek before the start of the tape |

For example: "The deck already holds game.uef; eject it before inserting other.uef."

The field `DeckStatus.tape_inserted` says whether a tape is in. It is not called `has_tape`
because protobuf derives `has_tape()` from the `tape` field.

The clients mirror the service. The Python client provides `bbc.cassette` and
`Beebium.boot_tape`, and the TypeScript client provides `Beebium.cassette` and `bootTape`. Each
boot call inserts the tape, types `*TAPE` and a load command (`CHAIN""` unless told otherwise),
and presses play.

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
| `tests/test_mc6850.cpp` | Real division at /1, /16 and /64; start-bit detection and mid-bit sampling; master reset clearing the control register; the /DCD latch and its clearing on a data read; the receiver held idle while /DCD is high |
| `tests/test_serial_ula.cpp` | The 16/13 MHz clock and its dividers; 64 RS423 clocks a bit at every rate; the read of &FE10; cassette mode's /DCD, /CTS and silence; the part |
| `tests/test_serial_ula_cassette.cpp` | The separator in ULA clocks: bursts, timeouts and the data rule. A tape byte arriving only at the matching ACIA divider. The detector: threshold for each part, re-arming by a 0, by motor off and by silence, and surviving latch writes. The VLSI inversion. The synthesiser's tones and silence; TDRE with no sink. The deck's movement, its refusal of a second tape, and Break |
| `tests/test_tape_signal.cpp` | Every UEF chunk the reader renders, refuses or skips; gzip; metadata; seek and duration; CSW against the same tape's UEF to within one clock, both versions, polarity, long pulses, and the refusals |
| `tests/test_cassette_mos.cpp` | With MOS 1.20 as a test vector: `*TAPE` and `CHAIN""` loading and running a BASIC program from UEF, at 300 baud after `*TAPE3`, and from CSW. `Data?` and `Rewind tape` for a bad data CRC; `Block?` for a block out of sequence. The Welcome tape's short leaders. `*CAT` with the tape running only while the relay is on; searching until the deck plays |
| `tests/test_grpc_cassette.cpp` | `CassetteService`: status on a fresh machine; insert, refusal of a second tape, and the error kinds; play gated by the relay; the status stream; a BASIC program loaded through MOS 1.20 |
| `tests/test_grpc_indicator.cpp` | The motor LED registered on every model after the lock LEDs, following latch bit 7 |
| `tests/test_preset_subcommands.cpp` | `resolve-preset` and `describe-preset-schema` for the deck; a preset's tape inserted stopped; `--tape` winning and playing; a bare name found in `tapes/` |
| `clients/beebium-python-client/tests/test_cassette.py` | `bbc.cassette` round trips, errors with kinds and sentences, the status stream, and `boot_tape` running a tokenised BASIC program |
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

- **Round 6:** recording, and blank tapes. This brings a recording transport state, a UEF
  writer, bundled blank-tape templates, `ListBlankTapes` and `CreateTape`, and the CLI's
  `list-blank-tapes` and `create-tape`.
- **Round 7:** WAV files, through a model of the machine's analogue front end.
- **Round 8:** live audio, streamed to the deck by a client from a host input device.
- **Later:** recording to CSW, selecting the VLSI part from a preset, and the GUI clients' deck,
  built against chapter 9.0 of the [client specification](client-specification.md).
