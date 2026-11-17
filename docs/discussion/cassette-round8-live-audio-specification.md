# Live audio: cassette round 8 specification (#208)

A client captures an audio input on its host and streams the samples to the server, which
plays them as the live tape through round 7's front end. After this round a real cassette
deck plugged into a line-in loads into Beebium. Programme: `cassette-specification.md`,
"Sampled sources and the front end" (Live PCM), "The deck" (Live source), "Service"
(`StreamAudio`). Microphone input is not a goal; the client captures from an input device the
user chooses and applies no voice processing.

## `LiveSource`

- A `SampledSource` fed by a lock-free single-producer single-consumer ring (moodycamel
  `ReaderWriterQueue` or the project's existing SPSC ring) of mono `float` samples, sized for
  2 seconds at the stream's rate. The gRPC thread produces; the emulation thread consumes, one
  sample per sample period of emulated time, as `SampledTape` does for a WAV.
- **Underrun** (`Decision`, programme): when the ring is empty the source yields silence (no
  edges) and counts the gap; the guest hears nothing, which the filing system reports as it
  would a dropout. **Overrun**: when the ring is full the oldest samples are dropped and
  counted; the guest sees a discontinuity. Neither is prevented and neither is hidden: both
  counts and the fill level are in the status.
- `duration()` is unknown (0); `seek` is refused with a sentence; the tape is not recordable.
- `LiveTape` (a `TapeSignal` over `LiveSource` and a `FrontEnd`) has no end: it ends when the
  stream closes, which ejects it.

## The deck with a live tape

- `Decision` (programme): the relay does not gate a live source. The deck plays the live tape
  whenever its transport is `Playing`, motor on or off; the ULA still re-arms its high-tone
  detector on the relay as always. `Play` and `Stop` are honoured (stopped: samples still
  arrive and are consumed into nothing, so the ring does not fill; on `Play` the deck hears
  what is arriving now). `Rewind`, `FastForward`, `Seek` and `Record` are refused with a
  sentence and `LIVE_SOURCE`.
- `position_seconds` counts samples consumed since the stream began, so a client's counter
  runs, and `duration_seconds` is 0.

## `CassetteService.StreamAudio`

```
rpc StreamAudio(stream AudioInputChunk) returns (StreamAudioSummary);

message AudioInputChunk {
    oneof payload {
        AudioInputFormat format = 1;   // the first message, once
        bytes samples = 2;             // then samples, little-endian, in the stated format
    }
}
message AudioInputFormat {
    uint32 sample_rate = 1;            // 8000 to 192000
    AudioSampleFormat sample_format = 2;
    uint32 channels = 3;               // 1 or 2
    string channel = 4;                // for 2 channels: "left" or "right"; absent means left
    string device_name = 5;            // the host device, for status display only
}
enum AudioSampleFormat { AUDIO_SAMPLE_FORMAT_UNSPECIFIED = 0; AUDIO_SAMPLE_FORMAT_S16 = 1; AUDIO_SAMPLE_FORMAT_F32 = 2; }
message StreamAudioSummary {
    uint64 samples_received = 1;
    uint64 samples_dropped = 2;        // overruns
    uint64 silence_samples_inserted = 3;  // underruns
    string error = 4;                  // a sentence when the stream was refused or failed
    CassetteErrorKind kind = 5;
}
```

Behaviour:

- The stream is the live tape. It is refused with `DECK_OCCUPIED` if a tape is in; its first
  message must be a format or it is refused with `INVALID_FORMAT`; a second format message
  ends the stream with `INVALID_FORMAT`. On a valid format the deck inserts the live tape,
  stopped, named after `device_name` or "Live input", and `DeckStatus` shows it (format
  `"LIVE"`, `sample_rate`, `channel`). The client then calls `Play`.
- Samples are converted to float on the gRPC thread, the chosen channel taken, and pushed to
  the ring. The gRPC thread never touches emulated state; the ring is the only seam
  (`docs/emulation-thread-ownership.md`).
- Closing the stream (client half-close, cancellation or connection loss) ejects the live tape
  and returns the summary. `EjectTape` on a live tape ends the stream from the server side.
- `DeckStatus` gains `live_ring_fill` (0 to 1), `live_overruns` and `live_underruns`
  (counters), present for a live tape; the stream of statuses updates them at
  `min_interval_ms` like the position.
- New error kinds: `LIVE_SOURCE` (an operation a live tape cannot do), `INVALID_FORMAT`.
- Timing: the ring absorbs the difference between the host's clock and emulated time; at real
  speed they drift by parts per million and 2 seconds of ring lasts hours. Below real speed
  (a slow host) the ring overruns at the rate of the shortfall; above (unlimited speed) it
  underruns. The status shows which. No resampling and no clock recovery (`Decision`: a
  live load at anything but real speed is not a goal; `feedback_real_speed_only_no_turbo`).

Fingerprint changes; four files; the Swift stubs regenerated (coordinate with me as in round
7). Python `bbc.cassette.stream_audio(samples, sample_rate, *, sample_format="f32",
channels=1, channel=None, device_name=None)` taking an iterable of `bytes` or NumPy-free
`array` chunks, returning the summary, and a `stream_wav(wav_filepath, realtime=True)` helper
that reads a WAV in blocks and paces them to wall-clock time, which makes the WAV corpus the
live test vector too; TypeScript the mirror with an async iterable.

## Clients

Round 8 is server, Python and TypeScript only. The GUI clients' live input (an input-device
picker listing the host's audio input devices, capture with no voice processing, the ring and
overrun/underrun shown, the macOS audio-input entitlement and usage string) is a separate GUI
round against the client-specification section this round writes:

- Client specification 9.0 gains "Live input": the picker lists input devices the host reports
  (names from the platform; no filtering by kind, since a line-in and a microphone are not
  distinguishable on every platform); choosing one opens `StreamAudio` with the device's
  native rate and format and inserts the live tape; the deck's keys behave as above with the
  refused ones disabled; the counter runs; the ring fill and the overrun and underrun counts
  are shown as a small meter or two numbers (LATITUDE); no voice processing (MANDATORY: no
  automatic gain, noise suppression or echo cancellation; on macOS this means the plain input
  format, not the voice-processing audio unit); stopping the stream ejects. The macOS app's
  audio-input entitlement and usage string are Rob's to supply (MACOS-ONLY).

## Tests

### Catch2, `tests/test_live_source.cpp`, `[cassette][live]`

1. Ring: a producer pushing a 2400 Hz sine at 48 kHz in 10 ms chunks and a consumer taking
   one sample per sample period yields edges 256 ± 2 clocks apart and no underrun or overrun
   counts; a producer that stops produces silence and an underrun count equal to the samples
   missed; a producer 10% too fast fills the ring and the overrun count grows at 10% of the
   rate; the drops are of the oldest samples (the newest are intact).
2. S16 and F32, mono and stereo with each channel chosen, give the same edges within one clock.

### Catch2, `tests/test_grpc_cassette.cpp`, `[grpc][cassette][live]`

3. `StreamAudio` with a tape in: `DECK_OCCUPIED`; with samples before a format:
   `INVALID_FORMAT`; a second format message ends it with `INVALID_FORMAT`.
4. A valid stream inserts the live tape, stopped, format `LIVE`; `Play` plays; `Seek`,
   `Rewind`, `FastForward` and `Record` give `LIVE_SOURCE`; `DeckStatus` carries the fill and
   counts; closing the stream ejects and returns the summary with the counts; `EjectTape`
   during a stream ends the stream.
5. MOS 1.20 as the test vector: `tape_ok` rendered as a 48 kHz WAV (90° sines) streamed in
   real time from a test thread while the machine runs at real speed; `*TAPE`, `CHAIN""`,
   `Play`; the program runs. The same with the motor relay opening and closing during the
   leader (the MOS blip) still loads: the relay does not stop a live tape.

### Python, `tests/test_cassette_live.py` and the corpus

6. `stream_audio` with a generated sine: the summary's counts; `stream_wav` of the builder's
   `tape_ok` WAV at real time loads through `boot_tape`-style steps; and in
   `test_cassette_corpus.py` the Plan B2 normal take streamed live (SKIP without the corpus),
   marked slow.

### Regression

7. Full C++ suite from a separate Release worktree; whole Python and TypeScript suites;
   fingerprint and README checks.

## Documentation

`docs/cassette.md`: "Live input" (the ring, the timing rule, the relay rule, what a live tape
refuses, the status fields, the summary); `grpc-server.md`; `client-specification.md` 9.0
"Live input"; CLAUDE.md and README ("live audio input" added to the cassette line); the
programme's Rounds section marks round 8 done and lists what remains (CSW recording, the VLSI
part in presets, the GUI live-input round).

## Delivery

Commits: (1) `LiveSource`, `LiveTape`, the deck's live rules, tests 1 and 2; (2) `StreamAudio`,
status fields, error kinds, stubs, fingerprint, tests 3 to 5; (3) Python and TypeScript, test
6; (4) docs. Full gate before reporting. The usual rules: `git commit --only`, never amend,
never push, 7-bit ASCII Catch2 names not starting with `--`, no host or people names, no AI or
model names in the body, your own trailer, `_filepath`/`_dirpath` suffixes. Stop and ask if
the ring's single-sample consumption shows up in the pacing statistics at more than 0.5% of
the emulation thread on this Mac, or if a `Decision` proves wrong.
