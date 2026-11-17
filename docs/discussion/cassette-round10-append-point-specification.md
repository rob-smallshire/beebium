# The append point: cassette round 10 specification (#208)

A key that takes the tape to just past the last recording, so the user can add to a tape
without scrubbing for the end of the blue. Rob's request (2026-11-17 night): "a symmetric
not-quite opposite of the back-to-the-beginning button, which moves to a second or two into the
last blank part of a tape, to facilitate easy appending". Programme: `cassette-specification.md`,
"Clients"; builds on round 9's signal map, which already tells the server where signal ends.

## The rule

- The **append point** of a tape is the end of the last second of its signal map whose level is
  not zero, plus a margin of `APPEND_MARGIN_SECONDS = 2` (`Decision`, a named constant). With
  level per second and the last non-zero second at index L, the point is `L + 1 + 2` seconds.
  The real last edge lies somewhere inside second L, so the blank run-in before the next
  recording is between 2 and 3 seconds: the gap a careful user would leave so the next leader
  does not butt against the previous block.
- A tape whose map is all zeros has its append point at 0, the same place Back to the
  beginning goes. A tape whose append point is at or past its end is seeked to the end and the
  deck stops there, as any seek past the end does; the response says so, and the user sees
  there is no room.
- "Signal" is whatever the map says it is. A tape full of hum above the comparator's threshold
  reads as recorded to its end; that is where a real deck's meter would say the recordings
  stop, and the key is honest about it. Long deliberate gaps between programs do not fool it:
  it finds the last recording, not the first gap.
- The server computes and performs the seek, so every client gets it for one call and the
  threshold lives in one place. Seeking ends a recording or a wind first, as `Seek` does.

## Service and clients

```
rpc SeekToAppendPoint(SeekToAppendPointRequest) returns (SeekToAppendPointResponse);

message SeekToAppendPointRequest {}
message SeekToAppendPointResponse {
    bool success = 1;
    string error = 2;               // a sentence when !success
    CassetteErrorKind kind = 3;
    double position_seconds = 4;    // where the head now is
    bool at_end = 5;                // the append point was at or past the end: no room
}
```

- Refusals, in-band like every cassette response: no tape `DECK_EMPTY`; a live tape
  `LIVE_SOURCE` ("A live input has no end to append to."); the map not yet computed
  `NOT_READY` (new `CassetteErrorKind` = 12: "The tape's signal map is not ready yet; try again
  in a moment."). The new kind is added to the macOS client's exhaustive
  `CassetteErrorKinds.swift` switch by the macOS developer in the GUI round; the server round
  lands the proto and stubs first, and the macOS build is red for that interval, which the
  architect sequences.
- `DeckStatus` is unchanged: the position moves as for any seek, and the clients' counters and
  markers follow. A client that wants to grey the key before the map is ready already has
  `signal_map_ready`.
- Python `bbc.cassette.seek_to_append_point() -> AppendPoint(position_seconds, at_end)`,
  raising `CassetteError` on refusal; TypeScript the mirror. The fingerprint changes (the same
  minor).

## Clients (the GUI round that follows)

Client specification 9.0, the deck keys: a **To the end of the recordings** key (the
skip-to-end mirror of Back to the beginning's icon) sits beside Back to the beginning at the
left edge of the row, so the two jump keys read as a pair and stay away from Eject; the centred
transport group and Eject are unchanged. It is momentary, never depressed, never coloured. It is
enabled while a tape is in and the signal map is ready, greyed with the server's sentence as
help text for a live tape and while the map is not ready. Pressing it seeks; the counter and the
marker follow; when the response says `at_end` the client shows the transport text the deck
reports (stopped at the end) and need say nothing more. Pressing it while recording ends the
recording, as any seek does.

## Tests

### Catch2, `tests/test_tape_signal_map.cpp` or `test_cassette_deck.cpp`, `[cassette][append]`

1. The builder UEF of round 9's test 1 (gap 2 s, carrier 3 s, data 2 s, gap 1 s, second block
   ending near 10.3 s): the append point is `floor(10.3) + 1 + 2 = 13` seconds, within one
   second of 12.3 + 2; a blank C15 gives 0; a tape whose signal runs to its last second gives
   the end with `at_end`; a map with a lone non-zero second near the end lands after it.
2. The deck: `seek_to_append_point()` on a playing deck lands at the point and keeps playing
   (as `Seek` does); on a recording deck it ends the recording first and the map's walk runs,
   then the seek uses the fresh map (the recording that just ended is counted).

### Catch2, `tests/test_grpc_cassette.cpp`, `[grpc][cassette][append]`

3. No tape `DECK_EMPTY`; live tape `LIVE_SOURCE`; before the map is ready `NOT_READY` (insert
   a long WAV and call at once, or hold the mapper with the test hook the round 9 tests used);
   a UEF with two blocks: `position_seconds` as test 1 and the status stream shows the new
   position; a full tape: `at_end` true, transport stopped at the end.
4. MOS 1.20 as the test vector: a blank C15, Record, `SAVE "A"` (round 6's machinery), Stop,
   `SeekToAppendPoint`, Record, `SAVE "B"`, Stop; then Back to the beginning, Play, `*CAT`
   shows both files and `CHAIN "B"` runs. This is the round's acceptance.

### Python, `tests/test_cassette.py`

5. `seek_to_append_point()` on the builder tape and on a blank; the `NOT_READY` and
   `LIVE_SOURCE` refusals raise `CassetteError` with the kind.

### Testing tiers (Rob, 2026-11-17)

Per commit: the targeted tests above and the full ctest when convenient. Before Rob pushes: the
full ctest, the Python and TypeScript cassette tests, fingerprint, README and proto checks,
from a separate worktree. The whole Python suite, the corpus and real-time live streaming are
CI's after the push; run them locally only if shared plumbing changes.

## Documentation

`docs/cassette.md`: "The append point" under the deck (the rule, the constant as a `Decision`,
the refusals); `grpc-server.md`; `client-specification.md` 9.0 (amended by the architect with
this round); the programme's Rounds section gains round 10.

## Delivery

Commits: (1) the deck's append point and tests 1 and 2; (2) the RPC, the new error kind, stubs,
fingerprint, tests 3 and 4; (3) Python and TypeScript, test 5; (4) docs. The usual rules:
`git commit --only`, never amend, never push, 7-bit ASCII Catch2 names not starting with `--`,
no host or people names, no AI or model names in the body, your own trailer,
`_filepath`/`_dirpath` suffixes, SKIP only in a test body, no POSIX-only calls. Tell the
architect when the stubs land, before the GUI round, because of the exhaustive Swift switch.
