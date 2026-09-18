# Audio sample playback evaluation (issue #82)

Phase 1: measurement and analysis of Beebium's SN76489 audio path against the
scarybeasts 3-channel 15 kHz sample player (`play_paradroid.ssd`), with beebjit
as the reference for how a faithful path behaves. No production code is changed
in this phase; this note records what was measured, what the defects are, and
what the fixes should be.

## Tooling built for this work

Under `tools/audio-analysis/`:

- `sn76489_baseband_experiment.cpp` -- a self-contained C++ harness that drives
  the real `beebium::Sn76489` exactly as a sample player does (tone 0 parked at
  period 1 as an ultrasonic carrier, its volume register rewritten at 15 kHz to
  encode a 1 kHz sine) and measures how much of that sine survives at baseband.
  It also runs an offline "ideal" reference chip (unipolar output, FIR low-pass,
  fractional decimation) for contrast. Build with the command in its header; it
  needs only the header-only core, no gRPC.
- `capture_disc.py` -- boots a disc in a real `beebium-model-b` server via the
  Python client and records the raw 48 kHz `AudioService` stream to WAV (a mono
  mix plus one WAV per channel). Runs under the client venv.
- `analysis.py` / `analyze.py` -- objective measures (spectrum, spectrogram,
  out-of-band energy, spurious tones, click detection, DC offset, and an aligned
  Beebium-vs-reference comparison with match-SNR) and a CLI over them.

WAVs and PNG plots are written to a scratch directory outside the repo (they are
not committed); regenerate them with the commands below. The plots referenced
here were produced at
`/private/tmp/claude-501/.../scratchpad/{wav,plots}/`.

## Headline result

The sampled-sound baseband is almost entirely absent from Beebium's output, and
what dominates instead is an aliased ultrasonic carrier.

- Synthetic experiment (`sn76489_baseband_experiment`): a 1 kHz tone encoded by
  15 kHz volume modulation over a period-1 carrier comes back at
  **0.00003** amplitude from the real Beebium chip versus **0.25209** from the
  ideal reference -- a **baseband recovery ratio of 0.0001 (0.01%)**. In the
  real-chip output **87.6%** of the energy sits above 7.5 kHz, and the single
  loudest spectral line is the aliased carrier at **14.5 kHz**, not the 1 kHz
  tone (which is at the noise floor).
- Real material (`play_paradroid.ssd`, 11.5 s captured): the loudest spectral
  peak is again the carrier alias at **14502 Hz**, **4.5 dB above** the actual
  musical content at ~521 Hz; **66%** of the energy is above 7.5 kHz.
- The only carrier filtering anywhere in the shipped path is in the macOS client
  (20 Hz high-pass + 8 kHz low-pass, `AudioRenderer.swift`). Emulating that chain
  on the Paradroid capture brings the music back to being the loudest component,
  but leaves in-band alias whistles at **4.5 kHz (-4.7 dB)** and **6.3 kHz
  (-4.9 dB)** -- only ~5 dB below the music -- because those alias products were
  folded below 8 kHz by the decimation *before* the filter runs, so no amount of
  8 kHz low-pass can remove them.

The raw Python-captured WAV (the independent-of-front-end recording the issue
asks for) has no filtering at all, so it is 66% out-of-band grit.

## The five questions

### 1. What the 250 kHz -> 48 kHz stage does, and its stopband

It point-samples. `Sn76489::tick()` advances the chip's internal state at 250 kHz
and, on a phase-accumulator schedule, calls `emit_sample()` which takes the
current instantaneous output (`src/core/src/Sn76489.cpp:124-139, 206-226`). There
is no averaging, no accumulation, and no low-pass -- a single 250 kHz sample is
copied out roughly every 5.2 internal ticks. A downstream sweep confirmed the
core `AudioBuffer`, the gRPC `AudioService`, and the Python client are all
verbatim passthrough; only the macOS `AudioRenderer` filters, and it does so at
48 kHz, after decimation.

Stopband: effectively none. Point sampling has no anti-alias response at all;
every component above 24 kHz folds back into the audible band with unity gain.
The 125 kHz carrier (a period-1 square toggles every internal tick) and the
images of the 15 kHz update rate alias directly onto audible frequencies -- this
is the measured 14.5 kHz line.

For contrast, beebjit (`~/Code/beebjit/sound.c`) runs a 4th-order Butterworth
low-pass (two cascaded biquads, `iir_lowpass_apply`) with a 7.2 kHz default
cutoff on the 250 kHz stream, *then* decimates with a fractional
accumulate-and-average resampler (`sound_resample_to_driver_buffer`) that even
splits the boundary sample into fractions. Its own comment: "Sampled sound
playback is very sensitive to the quality of downsampling."

### 2. Sub-output-sample timing of register writes

This part is actually fine in isolation. With the sound write gate open (the
common case after reset), every VIA Port A write is applied to the chip
immediately (`SystemViaPeripheral.hpp:88-90`), and the chip's state is stepped at
250 kHz, so a volume write takes effect at 250 kHz granularity -- well below one
48 kHz output sample. So writes are *not* quantised to output-sample boundaries.

The difference from beebjit is modelling depth, not timing resolution: beebjit
models the SN pulling the byte off the slow bus on its own clock (two edges,
with deliberate corruption if the bus changes between them;
`sound_advance_sn_timing`), whereas Beebium applies each write verbatim and
instantly. That is a fidelity gap for edge cases (short/again write gates, bus
races) but it is not the cause of the baseband loss and is a lower priority than
defects 1 and 3.

### 3. The volume-to-voltage (DC) model

This is the second decisive defect. `get_tone_normalized()`
(`src/core/src/Sn76489.cpp:257-273`) returns `128 +/- amplitude` -- a **bipolar**
square symmetric about a fixed mid-point of 128, for every volume. So the local
mean of the carrier is 128 regardless of the volume register, and volume
modulation moves no DC: the PCM baseband the technique depends on is never
created. The attenuation *law* is correct and matches beebjit (both are
-2 dB/step: Beebium `round(127*10^(-0.1v))`, beebjit
`round(8191*10^(-0.1v))`); the error is purely the missing offset.

The real chip, and beebjit, are effectively **unipolar**: the output is a level
proportional to volume when the flip-flop is high and a fixed silence level when
low (`sound.c:207-211`, silence = `volume_outputs[15]` = 0 by default). The mean
is then `level * duty`, which tracks the volume register -- that varying mean *is*
the recovered PCM.

`docs/sound-subsystem.md` describes exactly this varying mid-point as the reason
sampled sound works, but the shipped code does not implement it, and even the DC
metadata is wrong: `compute_voltage_levels()` returns a constant 0.8 V bias with
a swing symmetric about it (peak = 0.8 + swing/2, trough = 0.8 - swing/2), so the
"accurate reconstruction" metadata a front-end is told to use also has a
volume-independent mean. The doc's own table ("DC Bias 0.800" for every volume)
contradicts its prose.

### 4. Output filtering (high-pass for DC, low-pass for carrier)

Neither exists in the server or the Python path. The doc's pseudo-code implies
both; in the shipped code the only filters are the macOS client's 20 Hz
high-pass and 8 kHz low-pass biquads (`AudioRenderer.swift:199,202`,
`BiquadFilter.swift`). Two consequences: (a) any non-macOS consumer (the Python
capture, a future Windows/Linux front-end) gets an unfiltered, carrier-dominated
stream; (b) even the macOS low-pass is too late -- it runs at 48 kHz after the
aliasing has already folded carrier energy below 8 kHz, so it cannot remove the
in-band whistles measured at 4.5 and 6.3 kHz.

### 5. Buffering and drops

`AudioBuffer` holds 48000 samples (1 s) and `push()` drops silently when full
(`AudioBuffer.hpp:88-94`). The wire protocol cannot report a drop:
`AudioService::SubscribeAudio` sets `chunk.set_sequence(sequence++)` where
`sequence` is a **per-stream chunk counter**, not the buffer's produced-sample
count (`AudioService.hpp:69,84`). It therefore increments 0,1,2,... with no gap
whether or not samples were dropped, so the documented "sequence number for drop
detection" cannot detect drops. `cycle_count` is hardcoded to 0. `AudioBuffer`
does maintain a real produced-sample counter (`sequence()`), but it is never put
on the wire.

Measured: at real-time (1x) the gRPC consumer keeps up and nothing drops. At
unlimited speed the 1 s buffer overflows and samples are dropped with no signal
to the client -- which is why deterministic faster-than-real-time capture is not
currently possible without a seam (below).

## Ranked defects

1. **Bipolar output model destroys the sampled-sound baseband** (defect 3).
   Evidence: baseband recovery 0.01% of ideal; `get_tone_normalized` symmetric
   about 128. This is the primary cause of the material sounding wrong. Fix:
   model the output as unipolar (level-or-silence) so the carrier's local mean
   tracks the volume register.
2. **Point-sampling decimation with no anti-alias filter** (defect 1). Evidence:
   dominant 14.5 kHz alias line; 66-88% out-of-band energy. Fix: low-pass the
   250 kHz stream (e.g. beebjit's ~7.2 kHz 4th-order Butterworth) and decimate by
   fractional averaging rather than point sampling.
3. **No carrier/DC filtering in the server path; the only filter is macOS-only
   and too late** (defect 4). Fix: once decimation includes a proper low-pass,
   the carrier is gone before the wire; add a DC high-pass server-side (or emit
   the unipolar signal plus a documented bias) so every front-end is correct.
4. **Silent drops that the protocol cannot report** (defect 5). Fix: carry the
   `AudioBuffer` produced-sample count (or an explicit dropped-sample count) in
   `AudioChunk` so consumers can detect gaps; populate `cycle_count`.
5. **Bus-cadence write model is simplified** (defect 2). Lower priority; revisit
   after 1-3 if any material still mismatches beebjit.

Defects 1 and 2 compound: even a correct unipolar signal would still be wrecked
by point-sampling, and correct decimation of a bipolar signal still has no
baseband. Both must be fixed together to reproduce this material.

## Proposed fixes (design only -- not implemented in phase 1)

- Make the emitted per-channel sample unipolar: `silence` when the flip-flop is
  low, `level(volume)` when high, with `level` on the same -2 dB/step law.
  Decide the silence/peak convention so ordinary game audio keeps its current
  loudness (beebjit's `positive-silence` note is relevant: a large constant
  offset mixes badly, so DC should be removed before output).
- Insert an anti-alias low-pass at 250 kHz and a fractional-average decimator in
  `Sn76489::tick()`/`emit_sample()`, or move sample generation to a small
  resampler stage. Port beebjit's biquad coefficients as a starting point and
  re-measure out-of-band energy against a threshold.
- Add a server-side DC high-pass (or emit unipolar + bias metadata that is
  actually volume-dependent) and correct `docs/sound-subsystem.md` and
  `compute_voltage_levels()` to match.
- Extend `AudioChunk` with a produced-sample count for honest drop detection.
- Turn the two experiments into regression tests: assert baseband recovery above
  a threshold and out-of-band energy below one, on the synthetic tone and on a
  short committed passage.

## Remaining phase-1 work (not yet done)

- Deliverable 2 (register-write trace) and deliverable 3 (beebjit oracle WAV)
  require building beebjit locally and a small non-committed logging patch. The
  code comparison above already establishes *what* beebjit does differently and
  *why* it matters; the oracle capture would quantify the residual once defects
  1 and 3 are fixed. Flagged to the architect for go/no-go, since the decisive
  measurements (baseband recovery, alias dominance) are already in hand without
  it.

## Reproduce

```
# Synthetic baseband experiment
c++ -std=c++20 -O2 -I src/core/include \
    tools/audio-analysis/sn76489_baseband_experiment.cpp \
    src/core/src/Sn76489.cpp -o /tmp/sn_exp
/tmp/sn_exp <scratch>/wav

# Capture real material (real-time; needs a freshly built beebium-model-b)
export BEEBIUM_DISC_WORK_DIR=<scratch>/discwork
uv run --project clients/beebium-python-client \
    python tools/audio-analysis/capture_disc.py \
    --disc ~/Code/beebjit/test/sound/play_paradroid.ssd \
    --seconds 10 --out-dirpath <scratch>/wav

# Analyse
cd tools/audio-analysis
uv run python analyze.py measure <scratch>/wav/play_paradroid_mix.wav --plots <scratch>/plots
uv run python analyze.py compare <scratch>/wav/ideal_period1_1khz.wav \
    <scratch>/wav/beebium_period1_1khz.wav --plots <scratch>/plots
```
