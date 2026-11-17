# Sampled sources: cassette round 7 specification (#208)

The deck learns to play a WAV file: the signal is squared by a model of the machine's analogue
front end and delivered to the Serial ULA as edges, the same `TapeSignal` the UEF and CSW
sources produce. Programme: `cassette-specification.md`, "Sampled sources and the front end";
findings §1.5, §5.6, §7. Live audio (round 8) reuses everything here but the file reader.

## `SampledSource`

- `src/core/include/beebium/cassette/SampledSource.hpp`: mono samples as `float` in [-1, 1) at
  a rate in Hz, pulled in blocks (`read(std::span<float>) -> count`, `seek(sample)`,
  `sample_count()`, `rate()`). Two implementations in the programme; this round builds one.
- `WavSource`: RIFF/WAVE, PCM 8 (unsigned), 16, 24 and 32-bit integer and 32-bit float, any
  rate, mono or stereo (`WAVE_FORMAT_EXTENSIBLE` with those subformats too). Reads from the
  file on demand with a block cache; a 1 GB WAV does not load into memory. `Decision`
  (programme): for stereo the channel with the higher RMS over the first 10 seconds is used,
  unless a channel is chosen (`InsertTapeRequest.channel`, below). Anything else (ADPCM,
  µ-law, more than two channels, a truncated data chunk) is refused with a sentence and
  `UNSUPPORTED_FORMAT` or `INVALID_IMAGE`.

## `FrontEnd`

`src/core/include/beebium/cassette/FrontEnd.hpp`, a model of the LM324 stages (findings §1.5,
F7.1), run per sample at the source's rate, producing edges in tape time:

1. DC blocking: one-pole high-pass, 20 Hz.
2. Second-order high-pass at 1.29 kHz and second-order low-pass at 4.13 kHz, Butterworth
   biquads designed at the source's rate (bilinear transform) so the low tone's crossings move
   as on the machine (about +65° at 1200 Hz).
3. Comparator with hysteresis: the output flips when the filtered signal passes +h or -h, with
   h = 0.02 of full scale. No normalisation, no automatic gain.
4. Each flip is an edge at the sample's tape time, interpolated linearly between the two
   samples that straddle the threshold, so edge times are not quantised to the sample period.

The constants are `constexpr` parameters named in one place and asserted by tests, marked
`Decision (unverified)` as a group in `cassette.md`. The filter state resets on seek.

## `SampledTape`

A `TapeSignal` over a `SampledSource` and a `FrontEnd`: `next()` pulls samples until the front
end produces an edge or the source ends; tape time is sample index × (ULA clock rate / sample
rate), accumulated exactly as the other sources do. `duration()` from the sample count. Not
recordable. `kind()` reports `WAV`. `open_tape_image` recognises RIFF/WAVE by content.

## Service and clients

- `InsertTapeRequest` gains `optional string channel` (`"left"`, `"right"`, or absent for the
  RMS choice); `TapeMetadata.format` is `"WAV"`, and `TapeMetadata` gains `sample_rate` and
  `channel` (the one in use) so a client can show them. `GetDeckStatus` unchanged otherwise.
- Python `insert(path_or_url, channel=None)`; TypeScript the mirror. Fingerprint changes.
- The client specification's 9.0 adds `.wav` to the convenience filter and one sentence on the
  channel choice (LATITUDE whether the client offers it; the server's default is the RMS rule).
- `--tape` accepts a WAV. A preset may name one (`storage.cassette.image_uri`); it is copied
  into the machine directory like any media, so a preset that carries a 1 GB WAV is the user's
  choice to make.

## Performance

The front end costs a few tens of host cycles per sample at 48 kHz, about 0.1% to 0.3% of the
emulation thread's budget on a Pi (F7.5). Measure it: the pacing statistics (`docs/pacing.md`)
with a 48 kHz WAV playing versus idle, on this Mac, in the report. The Pi measurement waits for
Rob.

## Tests

### Catch2, `tests/test_front_end.cpp`, `[cassette][front_end]`

1. Filters: a 1200 Hz sine at 48 kHz passes with the expected phase shift (+65° ± 5°) and a
   2400 Hz sine with about -5°; 50 Hz hum at full scale produces no edges; 8 kHz at full scale
   produces no edges.
2. Comparator: a 1200 Hz sine at amplitude 0.05 produces edges at the rate 2400/s; at
   amplitude 0.01 it produces none; a 2400 Hz square at amplitude 1.0 produces 4800/s; noise at
   amplitude 0.015 produces none; the same noise under a 0.2 sine produces exactly the sine's
   edges.
3. Edge timing: a 2400 Hz sine's edges are 256 ± 2 clocks apart after interpolation at 48 kHz
   (without interpolation they would be 10 or 11 samples, 256 or 282 clocks).
4. Rates: the same signal at 22050, 44100, 48000 and 96000 Hz gives edge times within 2 clocks
   of each other.

### Catch2, `tests/test_tape_signal.cpp`, `[wav]`

5. `WavSource` reads every supported format (the test writes them with a small writer in the
   tape builder: `TapeBuilder::wav(rate, bits, channels, ...)`), refuses the unsupported ones
   with a sentence, chooses the louder channel, and honours a chosen one.
6. A UEF from the builder, rendered to a 48 kHz 16-bit WAV as sines (ideal tones, the
   builder), loads through `SampledTape` to the same cycle classes as the UEF (as round 6's
   round-trip test compares).

### Catch2, `tests/test_cassette_mos.cpp`, `[cassette][mos]`

7. `tape_ok` as a WAV at 48 kHz (sines, amplitude 0.5) loads and runs through MOS 1.20 with
   `--tape`; at amplitude 0.18 with added noise at 0.02 (the corpus's low take, F5.6) it loads;
   as a square wave (the clipped take) it loads; as a WAV of the UEF's signal shifted 90° as a
   sine it loads (T30963: a sine-like 90° signal loads on real hardware).

### Python, `tests/test_cassette_corpus.py`, the WAV corpus

8. Through `BEEBIUM_TAPE_CORPUS_DIRPATH` as now, SKIP when absent. Each case names the file,
   reads its TOML beside it for the signal channel, and loads through `boot_tape`:
   - every Plan B2 side-1 take at all three levels (normal, high, low) `CHAIN""`s its first
     program and reaches a screen string recorded by watching it once;
   - a Cascade 50 side-1 take `*CAT`s and `CHAIN`s a named program past the "bad patches" the
     TOML warns of, or the test documents which take and which program load cleanly and asserts
     that;
   - the Palace of Magic partial-rewind take loads after a `seek` to the position the test
     finds (the take has a play-stop-rewind-play in it; the test seeks past the rewound section
     and loads from there).
   Emulated-time budgets and the screen strings as in the UEF corpus cases. The corpus is
   third-party recordings, read in place, never copied or committed; the test names the
   files, not the people.

### Regression

9. Full C++ suite from a separate Release worktree; whole Python and TypeScript suites.

## Documentation

`docs/cassette.md`: "Sampled sources" (WAV, the front end's model and constants, the channel
rule, what is refused), the decisions-unverified group, and round 7 struck from "Not yet
built"; `grpc-server.md`, `cli.md`, `client-specification.md`, CLAUDE.md and README ("WAV"
added to the cassette line).

## Delivery

Commits: (1) `SampledSource`, `WavSource`, `FrontEnd`, `SampledTape`, builder WAV writer,
tests 1 to 6; (2) service, stubs, fingerprint, CLI, test 7; (3) Python and TypeScript, test 8;
(4) docs. Full gate before reporting. The usual rules: `git commit --only`, never amend, never
push, 7-bit ASCII Catch2 names not starting with `--`, no host or people names, no AI or model
names in the body, your own trailer, `_filepath`/`_dirpath` suffixes. Stop and ask if a corpus
take does not load with the programme's constants: report the take, what the pulse-length
histogram looks like through the front end, and which constant you would move, before moving
it.
