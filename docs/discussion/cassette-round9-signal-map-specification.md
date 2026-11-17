# Signal map: cassette round 9 specification (#208)

The deck's position timeline is coloured by where the tape carries signal: grey where it is
silent, blue where something is recorded, whether or not the something decodes or loads. Rob's
request (2026-11-17): "being able to visually see the silent and non-silent parts of the tape".
This is not a catalogue or a waveform, which stay excluded (programme, "Clients"): it tells the
user where the tape is quiet, not what is on it, and it is the one thing a real deck's user
could also learn by watching the level meter while winding.

## The map

- `TapeSignalMap`: one level per second of tape, `uint8` 0 to 255, the fraction of that second
  that carries signal. The tape's duration in seconds, rounded up, gives the length (a C90 is
  5400 bytes). A client draws it at pixel resolution by taking the maximum over the seconds a
  pixel spans, and thresholds it as it likes (the GUI round uses two colours; LATITUDE whether a
  client shades by level instead).
- Computed from the `TapeSignal` the deck would play, so every source is treated alike and no
  format is parsed twice: iterate `next()` from time 0 to the end; a second carries signal in
  proportion to the time within it that lies in a run of edges. `Decision`: a run is a sequence
  of edges whose spacing is below 4 ms (about 4,900 ULA clocks: a 300 baud zero's half cycle is
  1.67 ms, so every legitimate tone, carrier included, is a run; a `Gap` item or any longer
  spacing ends one). A second wholly within a run is 255; wholly outside, 0. Carrier (leader
  tone) is signal: it is not silent on a real tape, and the user is finding the start of a
  recording, which is where the leader begins.
- For a WAV the same walk runs the front end over the whole file, so silence is whatever the
  comparator's hysteresis (h = 0.02) rejects: a recording's noise floor, hum below the
  high-pass, or nothing. No separate amplitude threshold (`Decision`: one definition of
  "signal" for every source, the one the Serial ULA would hear). A 90-minute WAV is about
  260 M samples through the front end; measure it on this Mac and report; it runs off the
  emulation thread, so cost is latency to the map, not emulation time.
- Computed when a tape is inserted, on a worker thread, from a second `TapeSignal` opened on
  the same image (not the deck's: the deck's signal is the emulation thread's). Available
  through the status when done; until then the status says it is not ready. Recomputed when a
  recording ends (the span under the head changed), from the deck's current in-memory image,
  under the same rule. A live tape has no map (no duration).
- Not persisted; cheap enough to recompute on each insert (a UEF or CSW walk is milliseconds).
- **Live update while recording** (Rob, 2026-11-17 evening, after seeing the map change only
  when a scrub ended the recording): while the transport is `Recording` the deck keeps the map
  current in the arrow's wake. The deck sees every recorded edge and every clock of tape time
  with the relay closed, so it applies the same run rule incrementally to the second under the
  head: as the head moves, the level of the current second is the fraction of the part passed
  so far that lies in a run (so a second being written in silence falls to 0 as the head
  erases it, and one being written with tone rises to 255), and a completed second is final.
  The map is republished with `signal_map_generation` incremented at most once per second of
  tape time while recording (`Decision`: a named constant), and once more when the recording
  ends, when the full walk of the written-back image replaces the incremental map as the
  authority. Seconds the head has not reached keep their old levels. No client change: both
  GUIs refetch on a generation change already; the fetch is the map's few kilobytes a second.

## Service and clients

```
message TapeSignalMap {
    bool success = 1;              // false with no tape (DECK_EMPTY) or no cassette interface
    string error = 2;              // a sentence when !success
    CassetteErrorKind kind = 3;
    bool ready = 4;                // false while the map is being computed, or for a live tape
    uint32 seconds = 5;            // the number of levels, the tape's duration rounded up
    bytes levels = 6;              // one byte per second, 0 (silent) to 255 (signal throughout)
}
rpc GetTapeSignalMap(GetTapeSignalMapRequest) returns (TapeSignalMap);
```

- A separate RPC, not a `DeckStatus` field: the status streams many times a second and the map
  is kilobytes. `DeckStatus` gains `bool signal_map_ready = N` (and a `signal_map_generation`
  `uint32` that increments each time a map is published) so a client knows when to fetch it, or
  fetch it again after a recording. Errors are in-band like every other cassette response
  (`success`, `error`, `kind`): with no tape `DECK_EMPTY`; with a live tape `success` true and
  `ready` false. The walk runs on one worker thread the deck owns (latest job wins; an insert,
  eject or recording cancels a walk in progress and a stale walk never publishes; the deck's
  destructor cancels and joins it); the emulation thread only posts jobs.
- Python `bbc.cassette.signal_map() -> SignalMap(ready, seconds, levels: bytes)`; TypeScript the
  mirror. Fingerprint changes (already a minor).

## Clients (the GUI round that follows)

Client specification 9.0, the position indicator: the bar is coloured by the map when the
server reports it ready, grey where a second's level is below 128 and the accent colour (blue
on macOS; the theme's highlight on Linux) where it is at or above, at pixel resolution by the
maximum rule above; a plain bar until then and for a live tape. The map refreshes when
`signal_map_generation` changes. The bullet "What is not shown or heard" is amended: the map is
allowed because it shows where the tape is quiet, not what is on it; catalogue, waveform,
filenames and audio stay excluded.

## Tests

### Catch2, `tests/test_tape_signal.cpp` or a new `test_tape_signal_map.cpp`, `[cassette][signal_map]`

1. A builder UEF with 2 s of gap, 3 s of carrier, a 2 s data block, 1 s of gap, then a second
   block: the levels are 0,0 for the first two seconds, 255 through carrier and data, 0 for the
   gap second, with the boundary seconds in between (a second half in a gap and half in carrier
   is about 128 ± 10).
2. A 300 baud block is signal throughout (no zero's half cycle opens a gap).
3. The same UEF rendered as a WAV at 48 kHz (90° sines) gives the same map within ± 16 per
   second; the WAV with the gaps filled by noise at amplitude 0.01 still reads them as silent;
   at amplitude 0.5 full-scale hum at 50 Hz in the gaps still reads silent (the high-pass
   rejects it).
4. A CSW from the builder (or the same UEF converted) gives the same map.
5. Recording: a blank C15 maps to all zeros; after `Record` with the MOS saving a program
   (reuse round 6's recording test machinery) the map has signal where the recording landed and
   zeros elsewhere, and `signal_map_generation` advanced once.

### Catch2, `tests/test_grpc_cassette.cpp`, `[grpc][cassette][signal_map]`

6. `GetTapeSignalMap` with no tape: `DECK_EMPTY`; with a live tape: `ready` false; with a UEF:
   `ready` true within a bounded wait, `seconds` equals the duration rounded up, and the levels
   match test 1's expectations; `DeckStatus.signal_map_ready` turns true and the generation
   counts.

### Python, `tests/test_cassette.py`

7. `signal_map()` on the builder UEF and after a recording; SKIP-free.

### Performance

8. Report (not a test): the wall time of the map for `plan-b2-side1-level-normal.wav`
   (55 MB) and the longest Cascade 50 take (312 MB) from the corpus on this Mac, and the
   thread it runs on.

### Live update while recording

8a. Catch2 `[cassette][signal_map]`: a recording deck over a blank C15 with a tone synthesised
    for 3 s, silence for 2 s, tone for 1 s (round 6's machinery or direct `record_edge`
    calls): the generation advances about once per second of tape time (5 to 7 times over
    6 s), the map after each publish has 255 for completed tone seconds and 0 for completed
    silent seconds, the second under the head carries a partial level, and seconds ahead of
    the head are untouched; a recording over a tape that already had signal turns the erased
    seconds to 0 as the head passes. `[grpc][cassette][signal_map]`: the status stream shows
    the generation advancing during a recording, and `GetTapeSignalMap` between publishes is
    consistent with the last one.

### Regression

9. Full C++ suite from a separate Release worktree; whole Python and TypeScript suites;
   fingerprint and README checks.

## Documentation

`docs/cassette.md`: "Signal map" (the rule, the run threshold as a `Decision`, when it is
computed, the RPC); `grpc-server.md`; `client-specification.md` 9.0 (already amended by the
architect with this round); the programme's Rounds section gains round 9.

## Delivery

Commits: (1) the map computation and tests 1 to 5; (2) the RPC, status fields, stubs,
fingerprint, test 6; (3) Python and TypeScript, test 7; (4) docs with the performance figures.
Full gate before reporting. The usual rules: `git commit --only`, never amend, never push,
7-bit ASCII Catch2 names not starting with `--`, no host or people names, no AI or model names
in the body, your own trailer, `_filepath`/`_dirpath` suffixes, SKIP in the test body never
inside an assertion, POSIX-only calls nowhere. Stop and ask if the 4 ms run threshold
misclassifies a corpus take (report the take and the histogram) or if the whole-file WAV walk
takes more than 30 s for the 312 MB take on this Mac.
