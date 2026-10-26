# Music 5000 design

Status: proposed, 2026-10-26. Branch `music5000`. Owner: beebium-architect.
Implementation: beebium-music-5000.

This is the design for emulating the Hybrid Technology Music 5000 (and
its Music 3000 expander) as a Beebium peripheral extension. It rests on
three documents in `docs/music5000/`:

- `hardware-model.md`: the register-transfer description of the real
  board, taken from the original schematic. It is normative. Where this
  design simplifies the hardware it says so.
- `emulator-survey.md`: what the existing emulators do and which of them
  are independent.
- `stardot-key-points.md`: the designer's own statements about what an
  emulation must get right.

The facts about the framework it has to fit are in
`music5000-integration-questions.md` (same directory), which this
document answers. Two decisions were agreed with Rob on 2026-10-26 and
are not re-argued here: the 1 MHz bus port owns JIM paging, and the
device model includes the analogue output filter and the resampler, so
the server emits a single 48 kHz stream.

## 1. Goals and non-goals

Goals:

- A fresh model written from `hardware-model.md`, not a port of any
  existing emulator. The B-Em family, BeebEm and hoglet's FPGA are
  oracles for behaviour only. Nothing is transcribed from the FPGA (CC
  BY-NC-SA) and nothing from the GPL-2 emulators either; the model is
  GPL-3.0-or-later like the rest of Beebium.
- Sound that answers the designer's three points: the hardware's own
  analogue output filter, no bad resampling, and the TTL's actual
  behaviour (phase-set, amplitude latching at wrap, modulation pairing,
  the gate) rather than the simplifications the emulators disagree on.
- The Music 3000 as a second instance of the same extension, which is
  also the first time two JIM-paged devices share the 1 MHz bus.
- Coexistence with the SCSI host adapter on the same bus.

Non-goals:

- Sub-sample effects of the stereo chopper (channel staggering by up to
  20 us within a frame, left and right in consecutive slots). They are
  above the audio band after the filter and are documented as not
  modelled. The chopper is modelled as the exact per-channel left/right
  slot counts, 0..6 of 6.
- The Music 500's E-delay mod, the floating-bus value loaded by a read
  of `&FCFF`, and the electrical capture edge. Electrical only.
- A filter bypass or "clean" mode as a product option. The product is
  the hardware. A bypass may exist for tests.
- AMPLE, the Music 5000 ROM, or any software. Those are user-supplied
  sideways ROMs and discs as for any other add-on.

## 2. Framework changes (refactor first)

Each of these is a separate commit with its own tests, landed before
the device, and each changes no behaviour for existing extensions.

### 2.1 JIM paging in `OneMHzBusPort`

Today the port maps each of the 512 FRED/JIM offsets to one owner and
refuses overlapping claims (`OneMHzBusPort.cpp:37-43`). The real bus has
a different shape: every JIM-paged board latches the same paging
register at `&FCFF` and decodes only its own extended pages. The port
will model the bus, not the boards:

- The port owns the paging register. A write to FRED offset `0xFF`
  stores the byte; nothing else may claim `0xFF`, and a read of it
  returns the float value `0xFF` as today (no board drives the bus on
  reads; the Music 5000 drives nothing, and the port does not model the
  read-also-loads quirk, see `hardware-model.md` 1.4).
- JIM claims are by extended page. `claim_jim_pages(first_page,
  last_page, device)` registers a device for a contiguous range of the
  256 page numbers. Overlapping page claims throw, as offset claims do.
  The Music 5000 claims pages `0x30-0x3F`, the Music 3000 `0x50-0x5F`.
- Routing. A JIM access (offsets `0x100-0x1FF`) goes to the device that
  claims the current page, with the call `jim_read(page, offset)` /
  `jim_write(page, offset, value)` where `offset` is `0x00-0xFF` within
  the page. With no claimant the read returns `0xFF` and the write is
  dropped. FRED claims and dispatch are unchanged.
- `OneMHzBusDevice` gains `jim_read` / `jim_write` with default
  implementations (`0xFF`, drop), so FRED-only devices are untouched.
- The paging register is machine state: it is included in the device
  inspection surface and survives nothing (it is not cleared by reset,
  matching the Music 5000's latch, which has no reset; the port's
  register starts at `0xFF`).

Tests: page claim exclusivity; routing by page; a FRED device and a JIM
device coexisting; two JIM devices on disjoint page ranges; page bit 0
aliasing is the device's business (the Music 5000 ignores bit 0 in its
own decode: it claims all sixteen pages `0x30-0x3F` and uses bits 3-1).

### 2.2 Reset reaches the bus

`OneMHzBusDevice` gains `virtual void reset(ResetCause) {}`. The port
gains `reset(ResetCause)` which calls every attached device. The
hardware policies call it from `reset()` (PowerOn) and `soft_reset()`
(Break), next to `disc_socket.reset(...)`. The Advanced User Guide
confirms the bus's NRST line is asserted on power-up and on BREAK
(`docs/manuals_text/Advanced_User_Guide/28_28._The_One_Megahertz_bus.md`,
NRST pin 14), which settles `hardware-model.md` OPEN item 2: BREAK does
reset the synthesiser. The SCSI host adapter keeps its no-op default.

### 2.3 An audio source port for extensions

The SN76489 is today the only producer of `AudioBuffer` frames and
zeroes fields 2 and 3 (`Sn76489.cpp:217-223`). Two steps:

- **Extract frame assembly.** The SN76489 stops touching the frame
  layout. It hands its two 32-bit fields to a `AudioFrameAssembler`
  owned by the hardware policy, which builds the `AudioSample` and
  pushes it. No behaviour change; the SN76489 tests keep passing.
- **Add `AudioSourcePort`.** A port handle reachable through
  `ExtensionContext::get<AudioSourcePort>()`, offered by every machine
  that has an audio buffer. An extension registers one `AudioSource`
  per frame field it needs, naming the field index it wants (2 for the
  Music 5000, 3 for the Music 3000) and supplying the `AudioSource`
  metadata (name, encoding, channel names, group, and a new `bipolar`
  flag). Registration fails if the field is taken. The assembler, when
  it builds a frame, calls `uint32_t take_field()` on each registered
  source; a source that has no sample ready returns its previous value
  (the hardware's DAC holds its output).

Everything here runs on the emulation thread: the SN76489 tick, the
bus tick, and the frame push. No locking is added.
`docs/emulation-thread-ownership.md` applies unchanged.

### 2.4 `AudioFormat` becomes dynamic

`GetAudioFormat` describes the registered sources rather than a fixed
table. Fields with no source stay `ENCODING_SILENCE`. `AudioSource`
gains `bool bipolar = 6`: true for the Music 5000 (signed, zero is
silence, symmetric), false for the SN76489 (unipolar, as its comment
says). This is a proto change and so a fingerprint change; the next
release is already a minor bump for #228. The set of attached
extensions is fixed for the life of a server, so the format does not
change mid-session and no change notification is needed (integration
question 10).

## 3. The device

An extension `hybrid-music-5000` under `src/extensions/`, a plugin like
`acorn-scsi`, attaching to `1mhz-bus`. One parameter:

- `unit`: `"music-5000"` (default; page nibble 3, frame field 2) or
  `"music-3000"` (page nibble 5, frame field 3). A preset with both
  lists the extension twice, once per unit. The attachment catalogue's
  occupancy for `1mhz-bus` is already `0..N`.

No indicators: the unit has no LED (the panel has a power LED only,
which is not an activity indicator).

### 3.1 State

As the hardware, from `hardware-model.md`:

- Main RAM, 2 KiB: waves at `0x000-0x6FF` (fourteen tables of 128), the
  normal register bank at `0x700` and the alternate bank at `0x780`.
  Register rows as the survey's map.
- Phase RAM: per channel 24-bit phase and the stored amplitude byte.
- The INDEX flip-flop (bank select for the next-but-one counter value),
  and the DAC output registers' effect on the current sample.
- Initial contents are unspecified on the hardware. The model starts
  with RAM zeroed and phase RAM zeroed, which is one of the possible
  power-on states, and documents it.

### 3.2 Host interface

- `jim_write(page, offset, value)`: `ram[((page >> 1) & 7) << 8 | offset] = value`.
  Pages are claimed as `0x30-0x3F` (or `0x50-0x5F`); bit 0 of the page
  is ignored as on the board.
- Reads return `0xFF` (the port's default; the board is write-only).
- Latency: the real board lands a write within 2.4 us, inside one
  21.3 us sample. The model applies it immediately. Last write wins is
  automatic.
- `reset(cause)` for either cause: zero the normal bank `0x700-0x77F`,
  clear INDEX, and write zero into each channel's current wave byte as
  the hardware does (one byte per channel, `wave 0[phase_hi >> 1]` after
  the wave selects become zero). The alternate bank, the other wave
  bytes, the phase RAM and the amplitude stores are untouched.

### 3.3 The sequencer, per sample

The port ticks the device at 1 MHz. A sample period is 21 1/3 ticks, so
the device keeps an accumulator: add 3 per tick, and while it is at
least 64 subtract 64 and compute one frame of 16 channels. That is
46875 Hz exactly in emulated time, with no drift against the SN76489,
whose 48 kHz is derived from the same 2 MHz clock.

One frame computes the counter values k = 0..15 in order, which visits
register columns 0, 8, 1, 9, ..., 7, 15. For each counter value, with
bank = INDEX selected for it:

1. Read the channel's registers from the bank: freq lo/mid/hi, wave
   select (upper nibble), amplitude, control.
2. Phase. If freq lo bit 0 is set, phase := freq (all 24 bits, including
   that bit) and there is no wrap. Otherwise phase += freq (24-bit), and
   wrap = the carry out of bit 23.
3. Wave sample = `ram[wavesel << 7 | phase_hi >> 1]`: sign in bit 7,
   log magnitude in bits 6-0.
4. Amplitude store. If wrap, stored_amp := amplitude register. The
   stored value is what the DAC add uses, including on the wrapping
   sample itself.
5. DAC code. sum = sample_magnitude + stored_amp (8-bit, with the sign
   bit of the sample in bit 7 of the A operand as on the board). The
   gate opens only if bit 7 of the sum differs from the sample's sign
   bit. Open: code = sum[6:0] = chord sum[6:4], step sum[3:0]; linear
   value = `2^chord * (2 * step + 33) - 33` (0..8031); sign from the
   sample's sign XOR the control register's invert bit. Shut: zero.
6. Stereo. Position p = control bits 3-0. Left slots and right slots are
   the table in `hardware-model.md` section 6 (0-7: right 6; 8-10: left
   6; 11-15: left 5..1, right 1..5). Contribution to each side is
   `value * slots / 6`; kept in integer by accumulating `value * slots`
   and dividing the frame total by 6 once.
7. Modulation. The INDEX for counter value k+2 := control bit 5 of this
   channel AND (wrap OR sample positive), where "positive" is the sign
   bit before inversion. This gives the pairing column r modulates
   column r+1 within each group of eight, with 7 to 0 and 15 to 8 across
   the frame boundary.

The frame's two sums (signed, up to 16 * 8031 * 6 / 6 in magnitude) are
the DAC current for left and right, in units where 8031 is one
full-scale channel.

### 3.4 Analogue path

Applied per side, per 46875 Hz sample, in this order:

1. **Output filter.** The fourth-order network of `hardware-model.md`
   section 7 and its appendix: a complex pair at 3215 Hz with Q 0.667, a
   real pole at 3215 Hz and a real pole at 7717 Hz, DC gain normalised.
   Implemented as three cascaded IIR sections (two first-order, one
   second-order) discretised from those poles by the bilinear transform
   with frequency pre-warping at each pole, at 46875 Hz. The coefficients
   are computed in code from the component values (15K, 3n3, 1K, 22nF,
   the gain 1.5), with a comment giving the derivation, so a change in
   the schematic reading changes one place. The target response is the
   appendix table: -3 dB at 2115 Hz, -6 dB at 2933 Hz, -62 dB at
   Nyquist. Double precision state.
2. **Level.** Full scale: sixteen channels in phase on one side (the
   hardware's 1.6 V peak) maps to int16 full scale, so one channel at
   full amplitude is about 1/16 of full scale, as on the hardware. No
   automatic gain, no clipping logic beyond the int16 clamp, which
   cannot trigger at this mapping.
3. **Resampling to 48000 Hz.** Ratio 128/125. A polyphase FIR
   interpolator: upsample by 128, windowed-sinc low-pass (Kaiser, stop
   band at 23.4 kHz, at least 80 dB attenuation), decimate by 125. The
   filter is designed once at construction. Its cost is one dot product
   of a few dozen taps per output sample per side; the signal reaching
   it is already 62 dB down at Nyquist, so the resampler's own band
   edge is not critical and a modest tap count is enough. The chosen
   design and its measured passband ripple and stop-band attenuation
   are recorded in the component doc.
4. **Hand-off.** Each 48 kHz stereo pair goes into the device's
   `AudioSource` as a `pack_2x16bit(left, right)` field, taken by the
   frame assembler on the next frame. A ring of 16 entries absorbs the
   phase difference between the two exact clocks; it can neither
   overflow nor underrun in steady state, and the assembler repeats the
   last field if it ever finds the ring empty (start-up).

The filter runs before the resampler so that the resampler sees a
band-limited signal, which is also the physical order.

### 3.5 What is simplified, and why it is inaudible

- Chopper staggering and slot ordering (section 1).
- Write latency of up to 2.4 us (section 3.2).
- The I-to-V node's pole is part of the filter rather than per DAC
  terminal; with ideal op-amps the response from either terminal is the
  same (`hardware-model.md` 7.2).

## 4. Clients

- **Python**: decodes `AudioFormat` generically already. A helper to
  mix a chunk to float stereo by encoding and `bipolar` flag is useful
  for tests and is small.
- **macOS**: today copies only fields 0 and 1 and assumes the SN76489's
  unipolar layout in its renderer. It must honour the format: copy all
  fields, mix per field by encoding, apply DC removal only to unipolar
  sources. This is macos-client-developer's work, after the server
  lands, and is not required for the server milestones to be tested.
- **TypeScript**: no audio module; out of scope.

## 5. Tests

C++ (Catch2), `tests/test_music5000.cpp` and friends:

- Framework: the 2.1-2.4 tests listed above.
- Register map: writes through JIM pages land in the right RAM bytes;
  page bit 0 aliasing; the wrong page nibble is ignored; reads are
  `0xFF`.
- Sequencer, each against hand-computed expectations from the hardware
  model: phase accumulation and wrap; phase-set holds a constant level;
  amplitude takes effect at the first wrap after a write and on that
  sample; the gate shuts on underflow; the code law table (chord/step
  to 0..8031); invert; stereo slot counts; modulation pairing including
  7 to 0 and 15 to 8; reset zeroes the normal bank and leaves the
  alternate bank and phase RAM.
- Filter: impulse response, FFT, magnitude within 0.2 dB of the appendix
  table at 1, 2, 3.215, 5, 10, 20 kHz, and no peaking.
- Resampler: a 1 kHz sine in at 46875 Hz comes out at 48000 Hz with the
  right frequency, less than 0.1 dB level error, and all aliases below
  -80 dB.
- Rate: N emulated seconds produce exactly 48000 N frames in field 2
  with no repeats once past start-up.
- Two devices: Music 5000 at nibble 3 and Music 3000 at nibble 5 each
  receive only their own pages; a Music 5000 and the SCSI host adapter
  on one bus.

Python integration (`tests/test_music5000.py`): a BASIC program pokes
`&FCFF` and JIM to set up one channel with a sawtooth wave at a known
frequency, the client captures audio, and the test finds the fundamental
in field 2 within 1 Hz and checks the SN76489 fields are unaffected.
Timing asserts on emulated accounting, as always.

Oracles, by hand: B-Em with its filter on for the same register set (one
vote for the Beech family), and the real-hardware recordings linked in
the README for listening. The two column-wrap modulation pairings are
the one place the model knowingly differs from every emulator; the test
pins the hardware's pairing and the component doc says why.

## 6. Milestones

Each milestone is one or more commits on `music5000`, gated by targeted
tests; the full gate runs before the branch is merged to master
(fast-forward, Rob's call on timing).

- **M0 framework**: 2.1 JIM paging; 2.2 reset; 2.3 frame assembly
  extraction then the audio source port; 2.4 dynamic format with
  `bipolar` (proto change, stubs regenerated, all servers rebuilt).
- **M1 core**: the extension with sections 3.1-3.3, raw DAC sums into
  field 2 at 46875 Hz pushed as-is (a temporary nearest-sample hold to
  48 kHz for M1 only), and the C++ sequencer tests.
- **M2 analogue**: filter and resampler with their tests; the M1 hold
  removed.
- **M3 integration**: the Python test; `AudioFormat` verified over gRPC;
  the `music-3000` unit and the two-device tests; presets
  `model-b-music-5000` (and one with the SCSI adapter too).
- **M4 docs**: `docs/music-5000.md` component doc (hardware summary,
  the model, the filter and resampler designs with measured figures,
  the known simplifications), CLAUDE.md table row, README feature line,
  and a release note. Memory updated.

macOS client playback is a separate issue for macos-client-developer,
filed when M0 lands with the final `AudioFormat` shape.

## 7. Decisions recorded

- Port owns JIM paging; devices claim pages (Rob, 2026-10-26).
- Filter and resampler in the device model; single 48 kHz stream (Rob,
  2026-10-26).
- BREAK resets the synthesiser: the bus NRST line is asserted on BREAK
  (Advanced User Guide).
- Power-on RAM contents: zero, documented.
- Level mapping: sixteen channels in phase = int16 full scale.
- Music 3000 = second instance, parameter `unit`.
- No indicators, no filter bypass in the product.

## 8. Open

- `hardware-model.md` OPEN items 1, 3, 4, 5 and 7 stand. None blocks
  M0-M2. Item 4 (the modulation pairing at the wraps) is pinned by a
  test on the derived behaviour and revisited if a recording contradicts
  it.
- Whether the AM6070 datasheet is obtainable to confirm the code law
  and the E/D sense (left/right assignment). Rob may have one.
