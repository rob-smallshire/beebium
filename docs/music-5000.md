# The Music 5000

The Hybrid Music 5000 synthesiser, and its Music 3000 expander, as Beebium
builds them: a plugin on the 1 MHz bus whose sound joins the machine's audio
stream beside the SN76489's.

This is the component doc, as built: the hardware in brief, how the
synthesiser is integrated into Beebium as a plugin on the 1 MHz bus and a
source of audio frames, how to configure it, what the model computes, and
where the code and tests are. The normative description of the real board is
`docs/music5000/hardware-model.md`, written from the original schematic and
the AM6070 datasheet; the reference material it rests on, and the survey of
existing emulators, are in `docs/music5000/`. The design, with the decisions
and the reasons for them, is `docs/discussion/music5000-design.md`.
Companions: the extension framework (`docs/peripheral-extension-framework.md`)
for the bus claims and the audio source port, and `docs/sound-subsystem.md`
for the audio frames.

## The hardware

The Music 5000 (1986) is Hybrid Technology's development of the Acorn Music
500, designed by Chris Jordan. It is a box on the 1 MHz bus with sixteen
channels of 8-bit wavetable synthesis and a stereo analogue output. The
Music 500 and Music 87 are the same board, and the Music 3000 is a second
one answering to different pages, adding sixteen more channels.

### How software drives it

The board is reached through JIM, the 1 MHz bus's paged window:

- A write to `&FCFF`, the JIM paging register, selects an extended page. The
  Music 5000 answers to pages `&30`-`&3F` (the Music 3000 to `&50`-`&5F`).
  Page bits 3-1 choose one of eight 256-byte blocks of its 2 KiB RAM; bit 0
  is ignored.
- Writes to `&FD00`-`&FDFF` then go to that block.
- The board is write-only: it never drives the data bus, so reads float.

### The register map

The RAM holds fourteen 128-byte wave tables at `&000`-`&6FF` and two banks
of channel registers: normal at `&700` and alternate at `&780`. In each
bank, a row of sixteen bytes per register, one byte per channel (column):

| Row | Register |
|-----|----------|
| `+&00` | frequency low (bit 0: phase-set) |
| `+&10` | frequency middle |
| `+&20` | frequency high |
| `+&50` | wave select (upper nibble) |
| `+&60` | amplitude (log attenuation) |
| `+&70` | control: bit 5 modulate, bit 4 invert, bits 3-0 stereo position |

Wave bytes are sign and magnitude: bit 7 set is positive, bits 6-0 are a
logarithmic magnitude.

### The DSP

The board is a fixed-program processor built from LS TTL, clocked at 6 MHz:
eight instruction slots per channel, sixteen channels per sample, so 46875
samples a second. For each channel it adds the 24-bit frequency to a 24-bit
phase, looks up the wave at the phase's top seven bits, adds the amplitude
to the sample in the log domain, and feeds the result to an AM6070
companding DAC, whose current is steered left or right slot by slot by the
stereo position. Two op-amp stages filter each side: a fourth-order low
pass. `docs/music5000/hardware-model.md` traces every step to the ICs and
nets of the schematic.

## Architecture

The synthesiser lives in the server process, in the plugin
`hybrid-music-5000`, and nowhere else: clients never see registers or wave
tables, only the finished sound as one field of the machine's audio frames.
The path from the 6502 to a client's speaker:

```
6502 write to &FCFF / &FDxx
  |
  v
OneMHzBusPort (core; owns the JIM paging register, routes page+offset)
  |  jim_write(page, offset, value)          tick() at 1 MHz      reset(NRST)
  v
HybridMusic5000 (plugin: PeripheralExtension + OneMHzBusDevice + AudioFieldSource)
  |  3 ticks in 64 -> one 46875 Hz sample
  v
Music5000Synth  -> per side: Music5000OutputFilter -> level -> Music5000Resampler
  |  48 kHz (left, right) int16 pairs into a 16-entry ring
  v
AudioFrameAssembler (core; the SN76489 clocks it) ... take_field() once per frame
  |  AudioSample { field 0, field 1 = SN76489; field 2 = Music 5000; field 3 = Music 3000 }
  v
AudioService (gRPC): SubscribeAudio streams chunks of frames; GetAudioFormat describes each field
  |
  v
Clients: macOS mixes the field into its stereo output with a sub-master;
         Python and TypeScript decode fields generically
```

Everything above the gRPC boundary runs on the emulation thread, in
emulated time: the bus ticks the extension, the extension renders, the
assembler takes. Nothing in the plugin has a thread, a timer or a lock.

The plugin itself is three independent pieces and one class that wires them
to the framework:

| Piece | File (in `src/extensions/hybrid-music-5000/`) | Role |
|-------|-----------------------------------------------|------|
| `Music5000Synth` | `Music5000Synth.{hpp,cpp}` | The board's RAM, phase RAM, sequencer and DAC laws: the register-transfer model of `hardware-model.md`. Input: writes by page and offset; output: one (left, right) current pair per 46875 Hz sample |
| `Music5000OutputFilter` | `Music5000OutputFilter.{hpp,cpp}` | The fourth-order analogue network of schematic page 3 as a digital filter at 46875 Hz, one instance per side |
| `Music5000Resampler` | `Music5000Resampler.{hpp,cpp}` | 46875 to 48000 Hz, one instance per side |
| `HybridMusic5000` | `HybridMusic5000.{hpp,cpp}`, `plugin_entry.cpp`, `manifest.json` | The extension: claims the bus, registers the audio field, drives the three pieces from the bus tick, holds the delivery ring |

The three pieces have no Beebium dependencies and are tested in isolation.

### The framework it rests on

The Music 5000 is the first 1 MHz bus device that uses JIM and the first
extension that makes sound, so four things were added to the core before the
device was written (design section 2, "refactor first"). They are general,
not Music 5000 specific:

- **JIM paging belongs to the bus port.** `OneMHzBusPort` owns the paging
  register at `&FCFF` and routes every access to `&FD00`-`&FDFF` to the
  device that claimed the current page, via `claim_jim_pages(first, last)`
  and `jim_read` / `jim_write(page, offset, value)`. FRED claims
  (`claim_addresses`) are unchanged, so FRED and JIM devices coexist: the
  Acorn SCSI host adapter at `&FC40` and the Music 5000 on pages `&30`-`&3F`
  share one bus. The register is not cleared by reset, as on the real bus.
- **Reset reaches the bus.** `OneMHzBusDevice::reset(ResetCause)` is called
  on power-on and on BREAK from every hardware policy, standing in for the
  bus's NRST line (Advanced User Guide, pin 14).
- **An audio source port.** `AudioSourcePort`, owned by the hardware policy
  beside the SN76489, lets an extension register an `AudioFieldSource` for
  one of the frame's four 32-bit fields, with an `AudioFieldInfo` (name,
  encoding, channel names, group, bipolar). `AudioFrameAssembler`, extracted
  from the SN76489, completes a frame on each SN76489 output sample and asks
  each registered source for its field once per frame.
- **The audio format is dynamic.** `GetAudioFormat` is built from the port
  at call time (`describe_audio_format`), so a machine with the synthesiser
  describes three fields and one without describes two, and the format
  gained `bool bipolar` per source. That changed the protocol fingerprint.

These raised the plugin ABI to 9; every plugin in the tree is built against
it.

### The plugin

`manifest.json` declares the extension: name `hybrid-music-5000`, library
`hybrid-music-5000`, attaches to `1mhz-bus`, CLI stem `hybrid-music-5000`,
and one parameter, `unit`, a string with two choices:

| `unit` | JIM pages | Audio frame field | Source name | Group |
|--------|-----------|-------------------|-------------|-------|
| `music-5000` (default) | `&30`-`&3F` | 2 | `Music5000` | "Music 5000" |
| `music-3000` | `&50`-`&5F` | 3 | `Music3000` | "Music 3000" |

A machine with both boards lists the extension twice, once per unit. The
unit is read from the instance's configuration at `init`; any other value
is an error at launch.

`HybridMusic5000` implements three framework interfaces:

- **`PeripheralExtension`**: the manifest, `attaches_to()` (`1mhz-bus`),
  `init(ExtensionContext&)` and `shutdown()`.
- **`OneMHzBusDevice`**: `jim_write` goes straight to the synth's RAM;
  `jim_read` keeps the port's floating `&FF`, as the board never drives the
  data bus; `read`/`write` (FRED) are unused; `tick()` is the 1 MHz clock;
  `reset()` zeroes the normal register bank and the bank-select flip-flop as
  NRST does. It raises no interrupt.
- **`AudioFieldSource`**: `take_field()` returns the next 48 kHz
  (left, right) pair packed as the field's 32 bits.

Its life, in order:

1. The server's plugin loader finds the shared library on the extension
   search path, checks its ABI version and calls `beebium_create_extension`
   (`plugin_entry.cpp`), which constructs the object and hands it the
   manifest.
2. The machine assembly resolves `1mhz-bus`, applies the instance's
   parameters, and calls `init`. `init` claims the unit's pages on
   `ExtensionContext::get<OneMHzBusPort>()` and, if the machine has audio
   output (`ctx.has<AudioSourcePort>()`), registers itself for the unit's
   field with its `AudioFieldInfo`. A machine without audio output gets a
   silent but otherwise working board.
3. From then on the port ticks it at 1 MHz from the machine's main loop.
   Every 64 ticks it accumulates 3, and each time the accumulator passes 64
   it renders one sample: the synth gives a (left, right) current pair, each
   side goes through its filter, the level mapping and its resampler, and
   each 48 kHz output pair is pushed into the ring. The two resamplers run in
   lockstep, so their outputs pair up.
4. The assembler calls `take_field()` once per frame. The ring is primed to 8
   before delivery starts; on an underrun it hands back the last field and
   re-primes; on an overflow (nobody taking, which is audio output disabled)
   the oldest is dropped and delivery re-primes when it resumes.
5. `reset` on BREAK clears the registers as the board does; the ring and the
   filters are left alone, as the analogue path is.
6. `shutdown` has nothing to release; the loader never unloads the library.

### Where the bus clock comes from

The bus port is ticked from the machine's 1 MHz poll, so the synthesiser
advances in emulated time regardless of host speed, pause, or single-step.
The 46875 Hz sample clock is 3/64 of it exactly, and the resampler's 128/125
ratio is exact in integer arithmetic, so over any run the number of 48 kHz
samples the plugin produces equals the number of frames the SN76489 clocks,
to within the ring's prime level. There is no drift to correct and no
resampling by wall-clock.

## Configuring it

### Command line

Any 1 MHz bus machine takes the board as an extension option:

```bash
# A Model B with a Music 5000; the user supplies the AMPLE ROM and discs.
beebium-model-b --preset model-b-disc --hybrid-music-5000

# Both boards: a Music 5000 and a Music 3000 expander.
beebium-model-b --hybrid-music-5000 --hybrid-music-5000 unit=music-3000

# The shipped preset: B+ 128K, AMPLE fitted, booting into Studio 5000.
beebium-model-b-plus-128k --preset model-b-plus-128k-music-5000
```

The option follows the generic extension grammar (`docs/cli.md`,
Extensions): the stem, then `key=value` parameters. `unit` is the only
parameter. `describe-extension hybrid-music-5000` prints the schema, and
`list-extensions` and `start --help` list it with the others.

### Presets and the Configurator

In a preset file the board is an entry in `extensions` with
`"name": "hybrid-music-5000"` and, optionally, `"config": {"unit":
"music-3000"}`; `docs/preset-schema.md` has the form. The shipped preset is
generated from `src/server/CMakeLists.txt` with `add_plugin_preset`, which
depends on the plugin target so the plugin is deployed before the preset is
resolved, and carries its discs (next section).

The macOS New Machine dialog offers the board under the `1mhz-bus`
attachment point like any other extension, and the running machine's
Configurator shows the `unit` choice from the manifest. Nothing about the
synthesiser is configurable at run time: the board has no switches, and its
sound level is a mixer matter in the client.

### What is not configurable

There is no option for the output filter, the level mapping or the
resampler: they model the board's analogue path and its fixed clock. A user
who wants the sound of a clone with a gentler filter does not get it here
(see `docs/music5000/music5xxx-notes.md`); the model is the original.

## What Beebium models

### The DSP, per sample

The bus ticks the extension at 1 MHz; it renders one sample for every 64
ticks after adding 3 per tick, exactly 46875 per emulated second. Each
sample runs the sixteen channel counter values in the board's order, which
visits register columns 0, 8, 1, 9, ... 7, 15. For each:

1. **Phase.** Read the frequency from the bank in force. With freq lo bit 0
   set the phase is loaded with the whole frequency (bit 0 included) and
   does not wrap; otherwise the frequency is added and the carry out of bit
   23 is a wrap.
2. **Wave.** Read wave `select >> 4` at the new phase's bits 23-17.
3. **Amplitude.** On a wrap, latch the amplitude register into the stored
   amplitude; the latched value applies from the wrapping sample on.
4. **Gate.** Add sample and stored amplitude in eight bits. If bit 7 did
   not flip, the channel is silent this sample.
5. **DAC.** The low seven bits are the code: chord in bits 6-4, step in
   3-0. The sign is the sample's, inverted by control bit 4.
6. **Stereo.** The position gives the left share of six slots: positions
   0-7 all right, 8-10 all left, 11-15 left 5 down to 1. Each side
   accumulates current x slots, and the sample's total is divided by six.
7. **Modulation.** Control bit 5 AND (wrap OR positive sample, before
   inversion) selects the alternate bank for the counter value two on,
   which with the column order means column r modulates column r+1, and
   columns 7 and 15 modulate columns 0 and 8 in the next sample.

### The DAC laws

The AM6070's datasheet gives two laws, and the board uses both: its right
side is the chip's decode mode and its left side its encode mode, which
adds half a step (datasheet Tables 3 and 4):

| Side | Law | Range |
|------|-----|-------|
| Right (decode) | 2^C (2S + 33) - 33 | 0 to 8031 |
| Left (encode) | 2^C (2S + 34) - 33 | 1 to 8159 |

So a full-scale channel is 1.6 % louder on the left, and a gate-open code 0
on the left gives one unit rather than none.

### The output filter

The analogue network of page 3 of the schematic is fourth order, with no
zeros: a real pole at the I-to-V node (1K0 in parallel with 15K, with
22nF: 7717 Hz), a real pole in stage 1 (15K with 3n3: 3215 Hz) and the
Sallen-Key stage's pole pair (3215 Hz, Q 0.667). `Music5000OutputFilter`
builds its digital equivalent at 46875 Hz at construction, from those
component values:

- The poles are mapped by the matched z-transform.
- A two-zero numerator is fitted to the analogue magnitude. The fit is one
  3 x 3 least-squares solve in relative error, on 64 log-spaced points from
  20 Hz to 20 kHz.
- The numerator is factored to minimum phase, and the gain is set to unity
  at DC.

The fit's grid stops at the top of the audio band, not at Nyquist: carried
to Nyquist it would cost 0.6 dB at 20 kHz for no audible gain. A bilinear
transform was measured and rejected: its forced zero at Nyquist leaves it
4 dB low at 10 kHz and 38 dB low at 20 kHz.

The analogue magnitude up to Nyquist is the right target because the DAC
drives each channel as a pulse about 1 us long once per sample, not as a
held staircase. There is no zero-order-hold droop to model, and the images
around 46875 Hz fall below -80 dB after the network.

Measured against the analogue network from the same components:

| | Analogue network | Digital filter |
|-|------------------|----------------|
| -3 dB | 2115 Hz | 2118 Hz |
| -6 dB | 2933 Hz | 2937 Hz |
| Error, 20 Hz to 20 kHz | | within 0.1 dB |
| Peaking | none | none |

### The resampler

`Music5000Resampler` converts each side from 46875 Hz to the machine's
48000 Hz, the exact ratio 128/125, with a polyphase windowed-sinc
interpolator:

- 5120 taps at 6 MHz, 40 per phase.
- Kaiser window, beta 9; cutoff midway between the 20 kHz passband edge
  and the 26875 Hz stop-band edge, where the first image of 20 kHz content
  lands.
- Measured: 0.0005 dB passband ripple, 91 dB stop band. A 1 kHz tone comes
  through with every other bin at -114 dB.
- Group delay 426.6 us.
- Output times are kept in integer units of 1/128 sample, so N inputs give
  exactly ceil(N x 128/125) outputs, with no drift.

### Level and delivery

The level mapping puts sixteen full-scale channels in phase on the left
(16 x 8159) at int16 full scale, so one channel at full scale is about
1/16 of it, as on the board, whose output the article of the day called
"quite low in normal use". Nothing clips at this mapping.

The 48 kHz samples go into a 16-entry ring that the audio frame assembler
draws one field from per frame. Both 48 kHz clocks derive from the same
2 MHz, so the ring holds steady. It is primed to 8 samples before delivery
starts, which adds 8 / 48000 = 167 us of latency, and re-primes after an
underrun or an overflow. A full ring left while audio output was disabled
is trimmed to the prime level when delivery resumes. With nothing ready
(only at start-up) the field repeats its last sample, as the analogue
output holds.

End to end, a 1 kHz tone written through `&FCFF` and JIM arrives in field 2
at 1 kHz and within 0.003 dB of the DAC fundamental attenuated by the
network's -0.62 dB (`tests/test_music5000_extension.cpp`).

## Known simplifications

All are inaudible or outside the audio band:

- **Chopper timing.** Within each 1.33 us channel time the board drives
  its six slots left and right in turn, and the sixteen channels follow one
  another through the sample. The model keeps each side's share per sample
  and drops the sub-sample staggering (up to 20 us).
- **Write latency.** The board lands a host write within about 2.4 us; the
  model applies it at once.
- **Reset and modulation.** Reset also clears the modulation result
  waiting to reach the bank-select flip-flop. On the board that latch is
  reloaded with zeroed control registers before reset ends.
- **Reads of `&FCFF`.** On the board a read of `&FCFF` also loads the
  paging register from the floating bus. The port does not model it.
- **Polarity.** The AM6070 sinks current, so the board's output is
  inverted on both sides. The model keeps the code's sign.
- **Power-on state.** The board's RAM, phase RAM and stored amplitudes
  start random; the model starts them at zero.

## How it differs from existing emulators

The emulators surveyed in `docs/music5000/emulator-survey.md` share most of
the model; these are the places where the schematic and datasheet settled
something differently from some or all of them:

| Behaviour | Beebium (from the hardware) | Existing emulators |
|-----------|-----------------------------|--------------------|
| Phase-set bit | Loads the phase with the frequency | Some load it, some zero it |
| Amplitude change | Latched at the next wrap, applied on the wrapping sample | Some latch at wrap, some apply at once |
| Modulation pairing | Column r to r+1; 7 to 0 and 15 to 8, next sample | 7 to 8 and 15 to 0 |
| DAC law | Encode law (half a step more) on the left | Decode law on both sides |
| Output filter | The fourth-order page 3 network | None, or a generic third-order filter |

## Presets

| Preset | Machine |
|--------|---------|
| `model-b-plus-128k-music-5000` | B+ 128K, Music 5000, and AMPLE: the AMPLE Nucleus ROM in slot 4, its Help File ROM in slot 5, the Music 5000 System Disk in drive 0, Amplinex 001 and 002 in drive 1. Boots straight into AMPLE Studio 5000 |

The preset carries AMPLE, the software that drives the synthesiser: the Music
5000 System Disk and Amplinex 001/002 from Colin Fraser's Hybrid Music System
page, bundled by the project owner's decision, with the two ROM images
extracted from the system disc. Each machine works on its own copies of the
discs, so nothing it saves touches the masters. To fit the board to another
machine with your own AMPLE ROM and discs, give `--hybrid-music-5000` on its
own (see `docs/cli.md`). The system disc's `!BOOT` is AMPLE
Studio's own `runner` script without the Music 4000 module, and its boot
option is `*EXEC`, so power-on (or Shift-Break) goes straight to the Studio
5000 main menu. `packaging/discs/music5000-parts/PROVENANCE.md` gives the
source of each disc and every change made to it.

## Checking against the real software

In the B+ 128K preset, TAB at the Studio 5000 main menu opens AMPLE's command
line, and `"pavane" LOAD RUN` plays the system disc's Pavane (Faure,
programmed by David Reed). The system disc also has `concert`, `ppach`,
`rmix1`, `rmix2`, `kins1` and `kins2`. On Amplinex 001 in drive 1 the pieces
are `ALIEN`, `AMPLINX`, `BYSTAND`, `CONCERT` and `TOBLIN`, loaded the same way
as `":1.TOBLIN" LOAD RUN`. Amplinex 002 is drive 3. Files such as `VIOLA4`,
`BELL4` and `CLARIN` are instrument definitions and play nothing on their own.

`clients/beebium-python-client/tests/test_music5000_ample.py` does this in
about 15 s. It boots the preset to the menu, plays Pavane, and checks the
Music 5000's audio field:

- silent before the piece;
- loud while it plays: RMS 126 of 32767 measured, peak 657;
- zero-centred and unclipped;
- with 0.025 % of its power above 5 kHz.

To listen, `clients/beebium-python-client/examples/record_audio.py` records a
machine's audio to a WAV file:

```bash
uv run python examples/record_audio.py --preset model-b-plus-128k-music-5000 \
    --expect "Studio 5000 Main Menu" --type '\t"pavane" LOAD RUN' \
    --expect Pavane --seconds 30 --out pavane.wav
```

## A worked example

`clients/beebium-python-client/tests/test_music5000.py` drives the board
from BASIC exactly as software does. It selects page `&30` and writes a
square wave into wave 0:

```
?&FCFF=&30:FORI%=0TO127:I%?&FD00=&FF+(I%>63)*&80:NEXT
```

It then selects page `&3E`, the normal register bank, and sets column 0 to
wave 0, amplitude `&80`, control 0 (right side) and frequency 157482, which
is 157482 x 46875 / 2^24 = 439.9986 Hz. The test captures the machine's
audio and finds 440 Hz in field 2, within 1 Hz, measured on the 48 kHz
stream itself.

## Clients

`GetAudioFormat` describes each field with its encoding, channel names,
group and a `bipolar` flag: the Music 5000's field is bipolar (signed, zero
is silence, already through the board's AC-coupled output), the SN76489's
are not (unipolar, the client removes DC). A client that reads the format
needs nothing Music 5000 specific.

- **macOS** (`AudioClient.swift`, `AudioRenderer.swift`,
  `AudioMixerState.swift`, `AudioMixerView.swift`; #229, #230). The frame
  stream is split on arrival: fields 0 and 1 go to the SN76489 ring as
  before, and fields 2 and 3 to a second ring written in lockstep with it, so
  the two never drift apart and a stall in one is a stall in both. The
  renderer drains both rings every callback. A field the format marks
  bipolar 2 x int16 is summed into the stereo output at a fixed ratio,
  `musicMixGain` 0.85, with no DC removal; a server with no such field leaves
  the path inert and the output bit-identical to a machine without the
  board. The Sound sidebar shows one section per channel group the format
  reports, each with its own sub-master and mute under the master: the
  SN76489's, and a "Music 5000" (and "Music 3000") section when fitted. All
  sub-masters default to 1.0.
- **Python** (`beebium.client.audio`). `AudioFormat` carries `bipolar` per
  `AudioSource`; each `AudioChunk` unpacks its fields by their encodings, so
  a 2 x int16 field arrives as (left, right) int16 pairs.
  `examples/record_audio.py` records any field, or the mix, to a WAV file.
- **TypeScript**. Regenerated stubs carry the new field; no client logic
  depends on it.

Clients mix; they never change the sound. The server's field is the board's
output, and a client's gain, mute and sub-master are the user's amplifier.

## Source layout and tests

| Layer | Code | Tests |
|-------|------|-------|
| Bus port, JIM paging, reset | `src/core/include/beebium/extension/OneMHzBusPort.hpp`, `OneMHzBusDevice.hpp`, `src/core/extension-api/OneMHzBusPort.cpp` | `tests/test_one_mhz_bus_port.cpp`: FRED and JIM claims, overlap rejection, routing by the current page, floating unclaimed pages, two JIM devices, FRED and JIM coexisting, one tick per device. `tests/test_one_mhz_bus_reset.cpp`: reset reaches every device once, and from every hardware policy |
| Audio source port and frame assembly | `src/core/include/beebium/extension/AudioSourcePort.hpp`, `src/core/extension-api/AudioSourcePort.cpp`, `src/core/include/beebium/AudioFrameAssembler.hpp` | `tests/test_audio_source_port.cpp`, `tests/test_audio_frame_assembler.cpp` |
| Dynamic audio format | `src/service/include/beebium/service/AudioService.hpp` (`describe_audio_format`), `src/service/proto/audio.proto` | `tests/test_grpc_audio.cpp`, `clients/beebium-python-client/tests/test_audio.py` |
| Synth core | `src/extensions/hybrid-music-5000/Music5000Synth.{hpp,cpp}` | `tests/test_music5000_synth.cpp`, each case tagged with the `hardware-model.md` section it pins: page decoding, column order, 24-bit phase and wrap, phase-set, wave lookup, amplitude latched at the wrap, gate, both DAC laws and the left half step, inversion, stereo slots, the three modulation pairings, reset, and the sixteen-channel full-scale sum |
| Output filter | `Music5000OutputFilter.{hpp,cpp}` | `tests/test_music5000_output_filter.cpp`: the analogue response across the band, the -3 and -6 dB points, unity DC gain, no peaking, minimum-phase zeros, determinism |
| Resampler | `Music5000Resampler.{hpp,cpp}` | `tests/test_music5000_resampler.cpp`: exactly 128 outputs per 125 inputs without drift, unity DC gain, group delay, a clean 1 kHz tone |
| Extension | `HybridMusic5000.{hpp,cpp}`, `plugin_entry.cpp`, `manifest.json`, `CMakeLists.txt` | `tests/test_music5000_extension.cpp`: writes through pages `&30`-`&3F`, 46875 samples per emulated second, reset on power-on and BREAK, the two units and the SCSI adapter sharing the bus, an unknown unit rejected, the level mapping, 48000 samples per second without repeats, a 1 kHz tone end to end |
| Machine, from BASIC | | `clients/beebium-python-client/tests/test_music5000.py`: the format describes the board, 440 Hz written through JIM plays in field 2, a Music 3000 plays in field 3 |
| Machine, from AMPLE | preset `model-b-plus-128k-music-5000`, `discs/bundled/*.tar.xz`, `packaging/discs/build-music5000-discs.sh` | `clients/beebium-python-client/tests/test_music5000_ample.py`: boots to Studio 5000, plays Pavane, measures field 2 |
| macOS client | `clients/macos/Beebium/Beebium/Audio*.swift` | `clients/macos/Beebium/BeebiumTests/AudioRendererTests.swift`: lockstep rings, bipolar mix, sub-masters |
| Installed tree | `cmake/InstallSmokeTest.cmake`, `scripts/smoke-installed-tree.sh`, `packaging/homebrew/test-formula.sh`, `.github/workflows/windows-package.yml` | each expects the `hybrid-music-5000` plugin to be present and loadable |

The plugin is built as a shared library by `src/extensions/CMakeLists.txt`,
installed beside the other extensions, and listed in every packaging smoke
test, so a bundle missing it fails before release.

## Open items

From `docs/music5000/hardware-model.md` (its OPEN items 1, 3, 4 and 7), still open:

1. **Reads of `&FCFF`.** What value a read of `&FCFF` loads into the
   paging register on a real machine (not modelled; see above).
2. **Music 3000 decode.** Which board tracks change to make a board answer
   to page nibble 5.
3. **Modulation pairing at the wraps.** The pairing of columns 7 and 15 is
   derived from the board's timing. It is pinned by tests, and worth
   confirming against a recording of a real unit.
4. **Channel counter tap order.** The order of the bus taps is inferred
   from the counter's pin assignment, because the schematic leaves them
   unlabelled. It is consistent with the FPGA reconstruction.
