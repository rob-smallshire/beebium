# Recording and blank tapes: cassette round 6 specification (#208)

The deck learns to record what the Serial ULA transmits, and the server offers blank tapes as
it offers blank discs. After this round a user can SAVE from the Beeb to a tape of their own,
rewind, and CHAIN it back, in this machine or another. Programme: `cassette-specification.md`,
"Blank tapes and recording" and "The Serial ULA: transmit".

## The recordable tape

- `UefTape` becomes editable in memory: a tape of fixed length holding its edges in tape time
  (ULA clocks), replaceable over any span. Representation is yours, with two constraints: edge
  times are kept exactly (a recording read back gives the same edges within one clock), and a
  fully recorded C90 (about 26 million edges) stays under 100 MB. Runs of equal half-cycles,
  run-length coded, meet both. `CswTape` stays read-only.
- Metadata chunks (&0000 origin, &0001, &0003, &0005 and the other non-tape chunks the reader
  keeps) are carried through unchanged and written back first.
- `TapeSignal` gains `recordable()`; `CassetteDeck` gains `record()` and a further transport
  state, `Recording`, beside round 5's `Playing`, `Rewinding` and `FastForwarding`. `record()`
  on a read-only tape or an empty deck throws a sentence. Winding ends recording as it ends
  play; the state table in client-specification 9.0 gains the recording rows.

## Recording

- The deck records only while its transport is `Recording` **and** the relay is closed, exactly
  as it plays. Tape time advances as in play. Every clock that passes under the head while
  recording replaces what the tape held at that time with what the ULA's tone synthesiser
  produced: an edge, or nothing. RTS high, or RS423 selected, gives no edges, so a recording
  deck erases to silence while the Beeb is quiet, as a real deck does.
- The synthesiser's `TapeSink` is the deck. With the deck not recording, edges are discarded as
  today.
- Recording stops at the end of the tape (transport to `Stopped`), and `Stop`, `Seek`, `Rewind`,
  `FastForward`, eject and `Play` end it. Nothing is lost at a relay opening: the tape simply stops moving.
- The span recorded is noted (dirty from, to, union over a session) so that a tape never
  recorded on is never rewritten.

## Writing the UEF

The file is rewritten when the transport leaves `Recording`, on eject, on `SaveMachine`, and
at shutdown, if the tape is dirty; whole-file, atomically (write beside, rename over), gzipped
if the original was gzipped. The whole tape is re-emitted through one classifier so that
untouched and recorded spans are described alike; a tape loads back to the same edges within
one clock whatever chunk types the classifier chose, and this is the rule the documentation
states ("Beebium rewrites a recorded-on UEF in its own chunk vocabulary; its content is
preserved to the clock, its chunk layout is not").

The classifier walks the edges and emits, in order:

| What it sees | Chunk |
|---|---|
| no edge for longer than 2 × 512 clocks (two 1200 Hz half-cycles) | `&0116` gap, float32 seconds; `&0112` if an integer number of 1/(2 × 1200) cycles fits exactly |
| a run of 2400 Hz half-cycles (256 clocks ±5%) of even count | `&0110` carrier, split into chunks of at most 65535 cycles |
| a region of 1200 and 2400 Hz half-cycles that frames cleanly as 8N1 at the bit-cell rate (one 1200 Hz cycle or two 2400 Hz cycles per bit at 1200 baud; ×4 at 300 baud), start bits found where the preceding level is mark | `&0100` bytes, preceded by `&0117` when the baud differs from the one in force (1200 at the start of the file) |
| the same, framing cleanly at a format other than 8N1 (the ACIA's word select) | `&0104` with that format |
| a region that does not frame | `&0102` explicit tape data (1200-baud bits, one bit per 1200 Hz cycle or pair of 2400 Hz cycles), bit count and bits as the UEF specification says |
| a lone half-cycle or an off-nominal interval (the first or last half-cycle at an RTS transition) | absorbed into the neighbouring region; if it cannot be, `&0102` |

The synthesiser's tones change at zero crossings on 256/512-clock boundaries, so every
recorded interval is nominal and the classifier is deterministic; the tolerance exists for
spans that came from a CSW-shaped UEF. The reader gains `&0102` so that the writer's fallback
loads (test 2).

## Blank tapes

- `tapes/blank/<label>.uef`, committed, four of them: `Blank C15 (15 minutes).uef`,
  `Blank C30 (30 minutes).uef`, `Blank C60 (60 minutes).uef`, `Blank C90 (90 minutes).uef`.
  Each is UEF 0.10 with an `&0000` origin chunk reading `Beebium` and one `&0116` gap of the
  length in seconds (900.0, 1800.0, 3600.0, 5400.0; exact in float32). No date, no other chunk.
- `scripts/make_blank_tapes.py` writes them byte-reproducibly from a table and has `--check`
  (rebuild into a temporary directory, fail unless identical and the set matches), on the
  pattern of `scripts/make-blank-discs.sh`. Plain `python3`, standard library only.
- `src/server/CMakeLists.txt` copies them beside `blank-discs` into the build tree and
  installs them to `share/beebium/blank-tapes`; the server finds them as it finds the blank
  discs (the same lookup, a second directory name). Packaging picks them up through the
  install.

## `CassetteService` additions

```
rpc Record(RecordRequest) returns (RecordResponse);            // success, error, kind
rpc ListBlankTapes(ListBlankTapesRequest) returns (ListBlankTapesResponse);
rpc CreateTape(CreateTapeRequest) returns (CreateTapeResponse);

enum TapeTransport { ..., TAPE_TRANSPORT_RECORDING = 2; }
enum CassetteErrorKind { ..., CASSETTE_ERROR_KIND_READ_ONLY = 9; }   // record on a CSW (8 is INVALID_POSITION)
message TapeMetadata { ..., bool recordable = 8; }

message BlankTape { string label = 1; string filename = 2; string kind = 3 /* "tape" */;
                    uint64 size_bytes = 4; double duration_seconds = 5; }
message ListBlankTapesResponse { repeated BlankTape tapes = 1; }   // in file name order
enum CreateTapeErrorKind { UNSPECIFIED, UNKNOWN_LABEL, DESTINATION_EXISTS, WRITE_FAILED, WRONG_EXTENSION }
message CreateTapeRequest { string label = 1; string destination_path = 2; bool overwrite = 3; }
message CreateTapeResponse { bool success = 1; string error = 2; CreateTapeErrorKind kind = 3; string path = 4; }
```

`Record` acts at once; `DeckStatus.transport` reports `RECORDING`; a `Record` on a read-only
tape gives `READ_ONLY` with a sentence naming the tape and its format. `CreateTape` mirrors
`CreateDisc` exactly, including adding the template's extension when the destination lacks
one and refusing another. CLI subcommands `list-blank-tapes` and `create-tape` mirror the disc
ones. The fingerprint changes; four files.

## Clients

- Python: `bbc.cassette.record()`, `list_blank_tapes() -> list[BlankTape]`,
  `create_tape(label, destination_filepath, overwrite=False) -> Path`; `TapeTransport.RECORDING`;
  `CassetteErrorKind.READ_ONLY`; `CreateTapeError` with its kind. A README snippet: create a
  blank C15 in a temporary directory, insert, `*TAPE`, `SAVE "HELLO"`, record, wait, rewind,
  `CHAIN "HELLO"`.
- TypeScript: the mirror.
- Swift stubs regenerated; app builds.
- Client specification 9.0: the Record key (a deck's red key; LATITUDE whether it is a
  toggle or a key beside Play; MANDATORY that recording is a transport state the user selects
  and that `Stop` ends it) and the transport shown as Recording; a new subsection "Creating
  blank tapes", the mirror of 9.3, gated on a shared filesystem, with create and insert as
  distinct steps.

## Documentation

`docs/cassette.md` gains Recording (the rule above, the write-back moments, the classifier
table) and Blank tapes; `docs/grpc-server.md`, `docs/cli.md`, `docs/deployment.md`
(`blank-tapes` beside `blank-discs`), `docs/client-specification.md`, CLAUDE.md (recording and
blank tapes under Fully Implemented), README. The programme's Rounds section marks round 6
done.

## Tests

### Catch2, `tests/test_tape_recording.cpp`, `[uef][recording]`

1. Classifier, unit: carrier of 2n half-cycles gives one `&0110` of n; 70000 cycles give two
   chunks; a 900 s silence gives `&0116` 900.0; a gap of exactly 12 half-cycles of 1200 Hz
   gives `&0112` 12; a byte of 8N1 rendered by the builder gives `&0100` of that byte; the same
   at 300 baud gives `&0117` 300 then `&0100`; a 7E1 byte gives `&0104`; an odd bit sequence
   that frames as nothing gives `&0102` with those bits; metadata chunks come first unchanged.
2. Round trip: a recorded tape written and read back gives the same edge times within one
   clock, including an `&0102` region, and a tape that was gzipped is written gzipped.
3. Overwrite: a 60 s tape with carrier throughout, recorded over from 20 s to 30 s with
   silence, reads back as carrier, silence, carrier at those times; recording past the end
   stops at the end with the transport `Stopped`.
4. A tape never recorded on is not rewritten on eject (file bytes and mtime unchanged); one
   recorded on is rewritten on `Stop`, on eject and at shutdown (via the deck's flush), and
   atomically (no partial file visible on failure, simulated by an unwritable directory, with
   a sentence logged).
5. `record()` on a `CswTape` throws a sentence; on an empty deck likewise.
6. Blank tapes: `scripts/make_blank_tapes.py --check` passes (a ctest that runs it, as the
   blank discs have); each template loads, has the stated duration within one clock, and is
   recordable.

### Catch2, `tests/test_cassette_mos.cpp`, `[cassette][mos]`, MOS 1.20 as the test vector

7. Load `tape_ok` with `--tape`; insert the C15 template copied to a temporary path; `*TAPE`,
   `SAVE "OK"`, `Record`, RETURN at the RECORD prompt; wait in emulated time until the deck's
   position stops advancing after the tail; `Stop`; the written file's chunks are: origin,
   carrier of about 5.1 s, `&0100` header bytes beginning &2A and the name, carrier of about
   0.9 s between blocks, data, a final carrier; then `Seek(0)`, `NEW`, `CHAIN "OK"`, `Play`:
   the program runs (same screen check as round 4's test 11). Then a fresh Model B with
   `--tape <the written file>` runs it too.
8. The same at 300 baud (`*TAPE3`): the file carries `&0117` 300 before the data.

### Catch2, `tests/test_grpc_cassette.cpp`

9. `Record` with no tape: `DECK_EMPTY`; with a CSW: `READ_ONLY`; with a UEF: status
   `RECORDING`, and `Stop` ends it with the file rewritten.
10. `ListBlankTapes` lists the four by label in file name order with durations; `CreateTape`
    copies one, adds the extension, refuses `.csw`, refuses an existing destination without
    `overwrite` and replaces it with; `UNKNOWN_LABEL` for a made-up label; the created tape
    inserts and is recordable.

### Python and TypeScript

11. The client tests, the README snippet, drift check, fingerprint check; whole suites green.

### Regression

12. Full C++ suite from a separate Release worktree.

## Delivery

Commits: (1) the recordable tape, recording in the deck and ULA sink, the classifier and
writer, `&0102` in the reader, tests 1 to 5; (2) blank tapes, script, CMake, test 6;
(3) service, CLI, stubs, fingerprint, tests 7 to 10; (4) Python; (5) TypeScript; (6) docs and
client specification. Full gate before reporting. The usual rules: `git commit --only`, never
amend, never push, 7-bit ASCII Catch2 names not starting with `--`, no host or people names,
no AI or model names in the body, your own trailer, `_filepath`/`_dirpath` suffixes. Stop and
ask if the classifier meets a recorded interval it cannot place under the table above, or if
MOS 1.20's SAVE output does not frame as 8N1 (it should: that would mean a synthesiser or
ACIA transmit fault, which is a finding, not a tolerance to widen).
