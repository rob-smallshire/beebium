# The live input's meter and device changes: cassette round 12 specification (#208)

Round 11 unhid live input and gave it a level meter and a starve message. Rob proved the path on
2026-11-18, loading a program on the Beeb from a WAV played by another program through a virtual
audio device (`docs/live-input-routing.md`), and found two things on the way. Programme:
`cassette-specification.md`, "Clients"; builds on `cassette-round11-live-input-gui-specification.md`.
Client-only: no server, proto or fingerprint change.

## What Rob saw

1. The meter's clip mark lit red on the pop at the head of the tape (the corpus files open with a
   single full-scale click), the bar shot to the end of the scale, and within a second or two
   both had faded, leaving nothing that said what level the tape itself was running at. The bar
   is linear, so a healthy signal 20 dB down fills a tenth of it; at the half gain his virtual
   device happened to have, the meter read "a few pixels" for a tape that was perfectly loadable
   once the gain was right.
2. Changing the input device's sample rate in Audio MIDI Setup while a stream was open starved
   the server. Round 11's message fired correctly ("The live input is not keeping up with the
   Beeb; some of the tape is being made up as silence. Made up 2.3 s of silence since the stream
   began."), which is true but not the story: the device changed under the capture and the
   stream could never recover.

## The rulings

### 1. The meter reads in decibels, holds its peak, and marks only real clipping

- **Scale.** The bar is in decibels of full scale from `METER_FLOOR_DB = -60` to 0, linear in dB
  across its width (`Decision`, a named constant). A short tick marks `METER_TARGET_DB = -12`,
  the band a cassette signal should sit in: well clear of clipping, well above the front end's
  threshold. Silence and anything below the floor draw nothing.
- **Bar.** Follows the signal: instant attack, falls at `METER_BAR_FALL_DB_PER_S = 20` as now.
- **Peak hold.** A thin line sits at the highest level seen in the last `METER_HOLD_SECONDS = 2`
  and then falls at `METER_HOLD_FALL_DB_PER_S = 10` until the bar overtakes it. This is the line
  the user sets the source's volume by; the bar alone cannot be read while it moves.
- **Clip mark.** Lights only on sustained clipping: `CLIP_RUN_SAMPLES = 3` or more consecutive
  full-scale samples in the taken channel within one chunk (a single-sample click at a splice or
  the head of a tape is not clipping). It shows red for `CLIP_HOLD_SECONDS = 2` after the last
  clipping chunk and then disappears. There is no grey or dim state: the mark is either lit or
  absent, so its meaning is never "it clipped at some point".
- **Help text.** "The level of the live input, in decibels. Keep the held peak near the tick;
  turn the source down if the red mark appears."
- The pure per-chunk function grows a `clip_run` (longest run of consecutive full-scale samples
  in the taken channel) beside `peak`, and the hold, fall and dB conversion are a pure meter
  state type, tested; the drawing is not, as before. `CassetteClient`/`LevelMeterState` publish
  at the rate they do now (around 15 to 20 Hz), unchanged.

### 2. A device change ends the stream with a sentence, not a starve

When the chosen input device changes format (sample rate, channels) or goes away while a live
stream is open, the capture cannot continue with the format the server was given, and the right
outcome is a clean end with a reason, in the deck's failure slot with the 3.4 affordance:

- macOS: observe `AVAudioEngineConfigurationChange` for the capture engine (and the engine's
  input node format after it). On a change: cancel the stream (not the graceful half-close: there
  is nothing left to play out that is worth the wait), eject follows from the server as for any
  stream end, and show the failure "The input device changed while the live tape was playing;
  choose it again from Input Device." with the detail naming the device and the old and new
  formats ("BlackHole 2ch: 8000 Hz, 2 channels, now 48000 Hz"). If the device has gone (the
  engine's input node reports no channels, or the device id is no longer in the list), the
  sentence is "The input device was disconnected while the live tape was playing." with the
  device name in the detail.
- Linux: the honest equivalent through what Qt gives: `QAudioSource::stateChanged` into
  `StoppedState` with an `IOError`, and `QMediaDevices::audioInputsChanged` for a device that has
  gone. PipeWire does not report a format change on an open stream the way Core Audio does; a
  renegotiation surfaces as the source stopping, which the same handler catches. The sentences
  are the same two.
- Both: the starve detector is reset when the stream ends this way, and a device-change failure
  outranks a starve message that was raised in the same second (the starve was a symptom).
  Deduplication as in round 11: one message, replaced only by a different condition, cleared by
  Eject or the next stream.
- The server already ejects when the client cancels, and the next pick opens a fresh stream at
  the device's new format; nothing changes server-side.

## Client specification

Section 9.0's "Live input" bullet is amended: the meter is in decibels with a peak hold and a
sustained-clipping mark (MANDATORY shape; the constants LATITUDE within the named values); a
device format change or disconnection ends the stream with the sentence above (MANDATORY).

## Tests

Targeted per commit; the client's full unit suite before hand-back; nothing crosses the wire.

- Pure: dB conversion at the floor, the tick and full scale; bar fall rate; peak hold sits for
  two seconds then falls at 10 dB/s and is overtaken by a louder bar; clip run counting (two
  consecutive full-scale samples do not light it, three do; a full-scale sample in the other
  channel of a stereo chunk does not count; S16 at either rail and F32 at |x| >= 1); the clip
  mark holds two seconds and then is absent.
- Device change: a configuration-change event (fake engine or a posted notification on macOS; a
  stopped source with IOError on Linux) ends the stream, resets the starve detector, and raises
  the device-change failure; a starve raised in the same second does not outrank it; the next
  pick starts clean.
- View or widget: the meter shows the tick and the hold line for a live tape; the clip mark is
  absent (not grey) when not lit.

## Sequencing

Both client developers in parallel; the architect amends the client specification with this
document, sweeps each commit, and rebuilds Rob's Release app when the macOS commits land.
