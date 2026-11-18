# Live input in the GUI clients: cassette round 11 specification (#208)

Round 8 built live audio input end to end: the server's `StreamAudio`, its ring and timing
policy, and both GUI clients' capture and picker. Version 0.20.0 shipped it hidden, because the
macOS picker is gated on a microphone usage string that is Rob's to supply. Rob's rulings on
2026-11-18, reviewing the feature before scheduling it, are this round: what the live tape row
shows, how a buffering problem is reported, and the one string he adds. Programme:
`cassette-specification.md`, "Clients"; builds on `cassette-round8-live-audio-specification.md`.

## What the user is doing

A person has a real cassette player (or any other audio source) plugged into an input of the
host and wants the emulated BBC Micro to load from it. They choose that input from the deck's
Input Device menu, the deck shows a live tape, they press Play and type `*TAPE` and `CHAIN ""`
on the Beeb. The things that can go wrong for them are, in order of likelihood:

1. The level is wrong. Laptops have a single headset jack at microphone level and no line-in,
   so a player's headphone output clips unless turned well down; a quiet source does not lock
   the data separator at all. The user can act on this: turn the player up or down.
2. The operating system asks whether the app may use "the microphone" and the user, who
   plugged in a tape player, does not know why. On macOS the prompt fires once per app, on the
   first device picked, whatever that device is; Windows has one app-wide switch; Linux asks
   nothing. The user can act on this only if the prompt explains itself.
3. The host and the emulated machine drift apart faster than the server's two-second ring
   absorbs, so samples are dropped or made up. The user cannot act on this at all; the server
   already does the only sensible thing (drop the oldest, keep going).

Round 8's GUI reported the third and not the first two.

## The rulings

### 1. The buffer meter goes (both clients)

The live row's `Buffer 42% - over 0, under 0` line and its bar are removed. Rob: the numbers are
overly technical and not actionable. `DeckStatus.live_ring_fill`, `live_overruns` and
`live_underruns` stay in the protocol: the Python and TypeScript clients, the server tests and
anyone diagnosing a drift problem read them from the status stream; the GUI clients simply stop
drawing them.

### 2. A buffering problem is a deck message, deduplicated

When the ring has been failing to keep up, the deck says so once, in the same place and with
the same affordance a disc or tape error uses: the persistent failure marker of client
specification 3.4, whose full text can be copied and which the user dismisses when they have
read it, rather than a running count that clutters the sidebar.

- The condition is **sustained**, not instantaneous. The client raises the message when the
  overrun count or the underrun count has grown in each of the last `LIVE_STARVE_SECONDS = 3`
  consecutive seconds of status (`Decision`, a named constant in each client, tested). A
  single late chunk at the start of a stream, while the ring primes, never produces a message.
- Two messages, because the two conditions have different remedies for whoever does read them:
  underruns, "The live input is not keeping up with the Beeb; some of the tape is being made
  up as silence."; overruns, "The live input is arriving faster than the Beeb can take it;
  some of the tape is being dropped." These are client-authored sentences (3.4: capital and
  full stop); there is no server fragment to incorporate, since the server reports counts, not
  a refusal.
- **Deduplication.** The deck shows at most one message at a time, and an equivalent message
  never replaces a showing one. Equivalence is by condition, not by text: while an underrun
  message is showing, further underrun seconds change nothing, so the counts are not re-stated
  and the popover does not reopen. If the other condition then arises, the message is replaced
  (there is only one slot), so the user sees the latest kind. After the user dismisses a
  message, the same condition does not raise it again for the rest of that live stream unless
  it first clears for `LIVE_STARVE_SECONDS` and then recurs; the next stream starts clean. A
  message is also cleared by Eject and by the live tape ending, as any deck message is cleared
  by the tape leaving.
- **The affordance itself.** The disc error presentation is the model: the message stays until
  the user dismisses it, its full text can be copied, and it never truncates. The macOS deck
  already has this (the inline marker and its popover, `TransientMessage.showFailure`). The
  Linux deck row's message line wraps the sentence and has Dismiss and an Eject offer but no
  Copy; this round adds one (the sentence selectable by mouse and a Copy action, as the
  server-message dialog in `ErrorPresentation.cpp` has), so the deck's messages meet 3.4 in
  full on both clients, for every deck failure and not only this one.
- The counts are available to the curious in the message's detail text (the copyable part):
  "Dropped 1,204 samples since the stream began." or "Made up 0.8 s of silence since the
  stream began.", computed from the counts and the stream's rate at the moment the message is
  raised, not updated live.

### 3. A signal level meter replaces it (both clients)

The live row gains the one indicator a user can act on: a peak level meter with a clipping
mark, fed from the client's own capture tap, so it works when the server is on another host
and needs no change to the protocol. Clipping happens at the host's analogue-to-digital
converter, which is the client's side; the server only ever sees samples that have already
clipped, so a server-side level would be the wrong truth here.

- Peak of the absolute sample value over each capture chunk, decayed at a rate that reads
  well (a fall of about 20 dB per second is conventional; LATITUDE), drawn as a short bar in
  the position where the timeline sits for a file tape, since a live tape has no timeline.
- A clipping mark lights when any sample in the chunk is at full scale (S16 `-32768`/`32767`,
  F32 `|x| >= 1.0`) and holds for a second, so a transient clip is seen.
- Help text, as the deck's other controls have: "The level of the live input. Turn the source
  down if the mark at the right lights, up if the bar barely moves."
- The meter is computed by a pure, tested function per client (peak and clip of a chunk in
  each supported format), as `LiveAudioInput` on macOS and the chunker on Linux already are;
  the drawing is not tested.

### 4. Inputs stay unfiltered

The picker offers every input the operating system reports, under the name it gives, with no
filtering by kind. A line-in and a microphone cannot be told apart on every platform (laptops
have one headset jack at microphone level; desktop codecs name a blue line-in and a pink
mic-in; a USB audio interface is a generic input everywhere), so the client must not pretend
to. This was round 8's rule and stands.

### 5. The usage string is Rob's, and says why (macOS)

Rob adds `NSMicrophoneUsageDescription` to `clients/macos/Beebium/Beebium/Info.plist` himself,
in the release he chooses. Apple's dialog title is fixed ("Beebium would like to access the
microphone"); the body is this string, shown verbatim, and it must explain the purpose so a
user who plugged in a tape player understands the word "microphone": the chosen input is
played as a live cassette tape. The string also appears beside Beebium in System Settings,
Privacy & Security, Microphone, so it must read well out of context. The client does not add
it (the gate `LiveAudioInput.usageStringPresent` exists so the app never asks for the
microphone without it) and asks only when a device is picked, never at launch. The
`com.apple.security.device.audio-input` entitlement matters only under the App Sandbox, which
the app does not use; nothing to add today.

## Client specification changes

Section 9.0's "Live input" bullet is amended by this round: the fill and counts are no longer
shown (they remain in `DeckStatus`); a sustained starve or drop is a deck failure message with
the 3.4 affordance, deduplicated by condition; a client-side peak level meter with a clipping
mark is MANDATORY for a live tape; the usage string's purpose text is described.

## Tests

Per Rob's tiers: targeted per commit, the client's full unit suite before the round is handed
back, no real-server runs for this round (nothing crosses the wire).

- Pure: the starve detector (three consecutive growing seconds raise; two do not; a reset
  after a clear; the second condition replaces the first; a dismissed condition does not
  re-raise until it clears and recurs; a new stream starts clean). The detail text's numbers
  from counts and rate.
- Pure: peak and clip per chunk for S16 and F32, mono and stereo (left channel), including a
  full-scale sample in the right channel of a stereo chunk, which is not taken and must not
  light the mark.
- View or widget: the buffer line is absent for a live tape; the level meter is present for a
  live tape and absent for a file tape; a raised message shows the marker with Copy and
  Dismiss; the macOS popover opens once per raise (`failureToken`), not on every status.

## Sequencing

1. The architect amends `client-specification.md` 9.0 (this round, done with this document).
2. macOS and Linux developers implement in parallel; nothing in the server changes, so no
   fingerprint change and no sequencing between them.
3. Rob adds the usage string when he schedules the release; until then the picker stays hidden
   on macOS and the Linux client shows the feature as it does today.
4. The architect sweeps each commit and rebuilds Rob's Release app when the macOS commits land.
