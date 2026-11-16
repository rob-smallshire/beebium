# Service, indicator, persistence and clients: cassette round 5 specification (#208)

Round 4 left a deck in every machine that only `--tape` can load. This round gives it a
service, the keyboard's motor LED, a place in presets and machine directories, Python and
TypeScript clients, the client-specification chapter the GUI rounds build against, and the
as-built documentation. No tape format is added (WAV is round 6, live audio round 7) and the
emulation model does not change, except for one stale comment.

## `CassetteService`

`src/service/proto/cassette.proto`, package `beebium`, on the `DiscService` pattern. One deck,
so no drive number anywhere. Every error is a sentence naming the tape by the name the client
gave, with a kind.

| RPC | Request | Response |
|---|---|---|
| `InsertTape` | `url` (a `file://` URI or an absolute path, as `InsertDisc` takes) | `success`, `error`, `kind`, `tape` (a `TapeMetadata`) |
| `EjectTape` | nothing | `success`, `error`, `kind` |
| `Play` | nothing | `success`, `error`, `kind` |
| `Stop` | nothing | `success`, `error`, `kind` |
| `Seek` | `position_seconds` (double) | `success`, `error`, `kind` |
| `Rewind`, `FastForward` | nothing | `success`, `error`, `kind`; the deck winds backwards or forwards at 30 times real speed, ungated by the relay, until `Stop`, `Play`, `Seek`, eject or an end of the tape |
| `GetDeckStatus` | nothing | a `DeckStatus` |
| `SubscribeDeckStatus` | `min_interval_ms` (uint32, default 100) | `stream DeckStatus` |

```
enum CassetteErrorKind {
    CASSETTE_ERROR_KIND_UNSPECIFIED = 0;
    CASSETTE_ERROR_KIND_NO_CASSETTE_INTERFACE = 1;  // the model has no cassette interface
    CASSETTE_ERROR_KIND_CANNOT_OPEN = 2;            // missing, permissions
    CASSETTE_ERROR_KIND_UNREADABLE = 3;             // opened but could not be read to the end
    CASSETTE_ERROR_KIND_UNSUPPORTED_FORMAT = 4;     // not a UEF or CSW (round 6 adds WAV)
    CASSETTE_ERROR_KIND_INVALID_IMAGE = 5;          // recognised but refused with a sentence (a disc chunk, a bad version, an out-of-range value)
    CASSETTE_ERROR_KIND_DECK_OCCUPIED = 6;          // insert with a tape in
    CASSETTE_ERROR_KIND_DECK_EMPTY = 7;             // eject, play, stop or seek with no tape
}

enum TapeTransport { TAPE_TRANSPORT_STOPPED = 0; TAPE_TRANSPORT_PLAYING = 1;
                     TAPE_TRANSPORT_REWINDING = 3; TAPE_TRANSPORT_FAST_FORWARDING = 4; }  // 2 is round 6's RECORDING

message TapeMetadata {
    string name = 1;            // the file's name, as the client gave it
    string url = 2;             // the source, as given
    string format = 3;          // "UEF" or "CSW" (round 6: "WAV")
    string title = 4;           // from the image's metadata where it has one (UEF &0000 origin
    string origin = 5;          //   and &0001 instructions are not titles; use the UEF's
    string target_machine = 6;  //   target-machine chunk and inlay text only where the
                                //   findings say they are reliable; otherwise empty)
    double duration_seconds = 7;// the tape's length; 0 when unknown
}

message DeckStatus {
    bool has_cassette_interface = 1;  // false on a model without one (none today)
    bool has_tape = 2;
    TapeMetadata tape = 3;            // present when has_tape
    TapeTransport transport = 4;
    double position_seconds = 5;      // tape time, in seconds of the ULA clock
    bool motor_on = 6;                // the relay, latch bit 7, whether or not a tape is in
    string ula_part = 7;              // "ferranti" or "vlsi"
}
```

Behaviour:

- `InsertTape` opens the file off the emulation thread, judging the format by content as
  `open_tape_image` does, and swaps the deck's tape in under the pause/quiesce primitive of
  `docs/emulation-thread-ownership.md`. The deck is left stopped, at position 0. A tape already
  in gives `DECK_OCCUPIED` and the sentence names both tapes.
- `EjectTape` stops and removes, under quiesce. No quiescence wait: a tape has no write-back.
- `Play`, `Stop`, `Seek` act at once. `Play` at the end of the tape succeeds and leaves the deck
  stopped at the end, as a real deck's key would click back. `Seek` beyond the end lands at the
  end; a negative position is refused with a sentence.
- `GetDeckStatus` and the stream read the deck without stopping the machine: the deck
  publishes its transport, position and relay state through atomics it updates when they
  change, and the position at least every `min_interval_ms` of emulated time while playing
  (not on every clock). No mutex on the clock path (`feedback_no_defensive_mutexes`).
- `SubscribeDeckStatus` sends the current status at once, then a status on every change of
  presence, transport, relay or tape, and while the deck is playing the position no more often
  than `min_interval_ms`. It ends when the client cancels or the server stops.
- The relay is part of the status because a client shows it beside the transport ("Playing,
  motor off" is how a tape waits for the Beeb). The LED below is the indicator of record.

Registration in every server main beside `DiscService`. `docs/grpc-server.md` gains the service
table row and a section. The fingerprint changes; the four fingerprint files are updated
together and the next release after this lands is a minor.

## The motor LED

An indicator `cassette-motor-led`, label `Cassette motor`, colour red, registered in the hardware
policy beside the lock LEDs (`SystemViaPeripheral` registers `caps-lock-led`; the cassette LED
belongs to the Serial ULA's latch, so register it where the policy wires the ULA, in all seven
policies, before `Indicators` starts). Brightness 255 when latch bit 7 is 1, 0 when 0, whether
or not a tape is in. The 1 ms blips MOS makes are real: pass them through; the client applies its
drive-LED hold-off. `docs/indicators.md` lists it with the lock and drive LEDs.

## Presets and machine directories

- Preset schema: `storage.cassette: { "image_uri": path, URI or null }`, default `null`.
  Resolution, copying into a machine's directory and in-place opening follow the floppy rules
  of `docs/preset-schema.md` and `docs/machine-directory.md` exactly: find what `start`,
  `create-machine`, `resolve-preset`, `describe-preset-schema` and `SaveMachine` do for
  `storage.floppy_drives[].image_uri` and do the same for `storage.cassette.image_uri`.
  `resolve-preset` reports `image_uri` as a `file://` URI or `null`. `describe-preset-schema`'s
  `storage` section gains `cassette: { "has_cassette": true, "label": "Cassette", "image_types": [...] }`
  from the `has_cassette` trait and the formats `open_tape_image` reads. A model without the
  interface would give a warning at the key, as a built-in controller does for `fdc_socket`.
- A tape named by a preset is inserted at launch and the deck is left stopped. The `--tape`
  option wins over the preset (CLI over preset, as every other option) and keeps round 4's
  insert-and-play behaviour.
- `machine.json` gains `"cassette": { "position_seconds": 123.4 }`, written on `SaveMachine`
  (the position is state, like the screen and the battery-backed RAM, not configuration) and
  applied at `start --machine` when the preset's tape is inserted; absent means 0. Which tape
  is configuration and stays in `preset.json`'s `storage.cassette`. A tape inserted at run time
  through the service is not recorded on Save, as a run-time floppy is not; the position is
  written only when the deck holds the preset's own tape (compared by resolved source) and is
  otherwise omitted, so a stale position is cleared. The position is a `MachineRecord` field
  carried through every rewrite of `machine.json`, written by `SaveMachine` only, never at
  shutdown.
- A bare tape name is searched for in a per-user `tapes/` directory beside `discs/` and `roms/`
  (`ResourceKind::Tape`), documented in `docs/deployment.md`; a tape is not a disc. `--tape` is
  anchored as `--floppy` is, and `create-preset` writes it as `storage.cassette.image_uri`.
- `docs/preset-schema.md` and `docs/machine-directory.md` describe all of this as built.

## Python client

`clients/beebium-python-client/src/beebium/client/cassette.py`, `bbc.cassette`:

- `insert(path_or_url) -> TapeMetadata`, `eject()`, `play()`, `stop()`, `seek(seconds)`,
  `status() -> DeckStatus`, `events(min_interval_ms=100)` yielding `DeckStatus`; dataclasses
  `TapeMetadata`, `DeckStatus`, enums `TapeTransport`, `CassetteErrorKind`; `CassetteError` in
  `exceptions.py` with `.kind`, raised on any unsuccessful response, its message the server's
  sentence.
- `Beebium.boot_tape(tape_filepath, *, command='CHAIN""')` in the shape of `boot_disc`: insert,
  type `*TAPE` and the command, play, return at once with the machine running (the caller waits
  for the screen in emulated time as `test_cassette_corpus.py` does). A `*TAPE3`
  variant is the caller's `command`.
- Tests `tests/test_cassette.py`: insert/eject/play/stop/seek round trips, errors with kinds and
  sentences, the status stream's first message and a change, and `boot_tape` loading
  `tests/assets/cassette/tape_ok.bas`'s tape (built by the test from the `.bas` through the same
  route round 4's C++ test uses, or from a UEF the C++ test build writes into the build tree;
  prefer whichever keeps the Python test independent of the C++ build).
- README: a "Cassette" section with a tested snippet in `readme/snippets/test_readme_cassette.py`
  (the README is generated: template `readme/README.md.j2`, `scripts/generate_readme.py`, drift
  check `scripts/check_readme_regen.sh`). Regenerate.
- `test_cassette_corpus.py` moves to `boot_tape`.

## TypeScript client

`clients/beebium-typescript-client/src/cassette.ts`, the mirror: `insert`, `eject`, `play`,
`stop`, `seek`, `status`, `subscribe` (async iterable), `bootTape`; `CassetteError`; exported
from `index.ts`; tests in `tests/`; the README section. `tsc --noEmit` clean after the proto
regeneration (`feedback_ts_client_tsc_after_proto_change`).

## Swift stubs

`clients/macos`: regenerate `cassette.pb.swift` and `cassette.grpc.swift` with the pinned
protoc-gen-swift and protoc-gen-grpc-swift, add them to the project (`xcodegen generate`), and
confirm the app builds. No UI this round.

## Client specification

A new section **9.0 Cassette** at the head of chapter 9 (Storage), before 9.1, so the chapter's
order is cassette, floppy, hard disc, matching the sidebar. Profile: Core. Binding document:
`docs/cassette.md`. Service: `cassette.proto CassetteService`. It states, as behaviour,
conformance and scenarios in the chapter's house style:

- **Placement**: in the storage sidebar the cassette section sits above the floppy drives, which
  sit above hard discs (`feedback_cassette_above_floppies_in_storage_sidebar`). MANDATORY.
- **Insert**: browse, drag and drop, and the recent-discs machinery extended to tapes (one shared
  recents list or a separate one is LATITUDE; the recording policy of 9.2 is MANDATORY). The
  server judges the format; the client filters its file dialogue by extension only as a
  convenience and never refuses a file itself.
- **The deck keys** (Rob's concessions, 2026-11-16): Play, Stop, Rewind and Fast forward (the
  winding transports; the counter runs while they do), a Back-to-the-beginning button
  (`Seek(0)`), Eject, a **tape counter** showing the position as minutes and seconds, and a
  position slider from `position_seconds` and `duration_seconds` the user scrubs with (seek on
  release; dragging while playing seeks). The counter and the slider are the only guide to
  where anything is on the tape, as on a real deck. The transport and the relay are shown. An insert-and-play gesture (a double-click, a
  modifier on drop) is LATITUDE; the deck starts stopped after a plain insert. MANDATORY: play
  is separate from insert.
- **What is not shown**: no block catalogue, no waveform, no decoded filenames. MANDATORY, from
  Rob's ruling.
- **Errors**: the server's sentence, in the register of 3.4; the kind selects only the icon or
  the recovery control.
- **Indicator**: the motor LED is rendered through chapter 10 like every other LED; the deck
  view does not duplicate it.
- **The configurator**: the Storage section of the New Machine configurator (5.2) gains a
  Cassette row above the drives: browse and drop a tape, written as `storage.cassette`,
  validated through `resolve-preset` like a floppy, shown only when `describe-preset-schema`
  reports `has_cassette`.
- **Live input**: a line reserving the input-device picker for round 7 ("audio input devices the
  host lists; no voice processing; the macOS app needs the audio-input entitlement").

Appendix A gains the service; Appendix B the profile line. Section 1's capability index gains
the entries. Section 5.2 and 5.3 get one sentence each on the cassette row and the recorded
position.

## Documentation

- `docs/cassette.md`, new: the cassette interface as built, in the form of `docs/analogue-port.md`:
  the hardware (ULA, ACIA, relay, LED) in summary with pointers to `serial-acia.md`; the clock
  model and the parameter table from the programme (separator, detector, synthesiser, part);
  tape sources (`TapeSignal`, UEF and CSW rules, what is refused); the deck; the service; the
  LED; presets and machine directories; `--tape`; testing (the built tapes, MOS 1.20 as a test
  vector, the corpus through `BEEBIUM_TAPE_CORPUS_DIRPATH`); decisions still unverified; what is
  not yet built (WAV, live audio, recording). It does not name any person or host.
- `docs/serial-acia.md`: the cassette side points to `cassette.md`.
- `docs/grpc-server.md`, `docs/indicators.md`, `docs/preset-schema.md`,
  `docs/machine-directory.md`, `docs/cli.md` (`--tape`), `docs/client-specification.md` as above.
- CLAUDE.md: `CassetteService | cassette.proto` in the services table; `cassette.md` in the
  documentation list; "cassette interface (UEF, CSW)" under Fully Implemented; the service count.
- README: a feature line.
- `docs/discussion/cassette-specification.md`: the Rounds section marks 3, 4 and 5 done, and the
  "machine.json records the tape and position" line is corrected to what this round built.
- `src/core/include/beebium/serial/SerialUla.hpp`: the header comment on the detector still
  says it "holds through silence with the motor on"; make it say silence re-arms it
  (`feedback_comments_describe_present`).

## Tests

### Catch2, `tests/test_grpc_cassette.cpp`, tag `[grpc][cassette]`

1. `GetDeckStatus` on a fresh machine: interface present, no tape, stopped, position 0, motor
   off, part `ferranti`.
2. `InsertTape` of a built UEF: success, metadata (name, format `UEF`, duration within one
   half-cycle of the builder's), deck stopped at 0; a second insert gives `DECK_OCCUPIED` with a
   sentence naming both; `EjectTape`, then `Play` gives `DECK_EMPTY`.
3. `InsertTape` of a missing file, an empty file, a non-tape file and a UEF with a &0200 chunk
   give `CANNOT_OPEN`, `UNREADABLE` or `UNSUPPORTED_FORMAT`, `UNSUPPORTED_FORMAT`, and
   `INVALID_IMAGE`, each with a sentence containing the file's name.
4. `Play` with the motor off advances nothing in a second of emulated time; `*TAPE` then
   `CHAIN""` typed closes the relay and the position advances; `Stop` holds it; `Seek` to the
   duration leaves it stopped at the end; `Play` there stays stopped.
4a. `Rewind` from 60 s reaches 0 after 2 s of emulated time and stops; `FastForward` from 0 on a
   90 s tape stops at 90 s after 3 s; `Stop` mid-wind holds the position; `Play` mid-wind plays
   from there; the relay open does not stop a wind; the ULA receives no edges while winding.
5. `SubscribeDeckStatus(50)`: the first message is the current status; an insert, a play, a
   relay closure and a stop each produce a message in order; while playing with the relay closed
   two consecutive position messages are at least 50 ms of emulated time apart.
6. The MOS load of round 4's test 11 through the service instead of `--tape`: insert, type,
   play, program runs. Model B, with the same `tape_ok` tape.

### Catch2, `tests/test_grpc_indicator.cpp` or a new `[indicator][cassette]` case

7. `ListIndicators` on every variant includes `cassette-motor-led` after the lock LEDs with the
   label and colour; a write of &FE to &FE10 raises it to 255 and &7E drops it to 0; a
   subscription sees both.

### Catch2, presets, `tests/test_preset*.cpp` as the existing floppy cases are laid out

8. `resolve-preset` on a preset with `storage.cassette.image_uri` naming a tape beside it gives a
   `file://` URI; a missing tape is a warning at `/storage/cassette/image_uri` with a sentence, as a
   missing floppy is; a
   preset without the key resolves to `null`; `describe-preset-schema`'s storage section has the
   `cassette` entry on every model.
9. A machine created from a preset with a tape starts with that tape inserted and stopped; with
   `--tape` as well, the CLI's tape is inserted and playing.
10. `SaveMachine` with the deck at 12.5 s writes `cassette.position_seconds` within 0.01 of it;
    `start --machine` resumes at that position, stopped.

### Python and TypeScript

11. The client tests above; the whole Python suite in sequential chunks and the TypeScript
    suite green; README drift check clean; fingerprint check
    (`scripts/sync_protocol_fingerprint.py --check`) clean.

### Regression

12. The full C++ suite, the serial suites in particular, green from a separate Release worktree.

## Delivery

Commits in this order, each with its targeted tests: (1) the proto, service, server
registration, stubs for all three clients, fingerprint files, `test_grpc_cassette.cpp`;
(2) the LED with test 7; (3) presets and machine directory with tests 8 to 10; (4) Python client
and README; (5) TypeScript client; (6) documentation and the client-specification chapter. Full
gate before reporting done. The usual rules: `git commit --only` (`git add` new files first),
never amend, never push, 7-bit ASCII Catch2 names not starting with `--`, no host or people
names, no AI or model names in the body, your own trailer, `_filepath`/`_dirpath` suffixes.
Stop and ask if the floppy rules for presets and Save turn out not to fit a tape, or if a
`Decision` in the programme proves wrong.
