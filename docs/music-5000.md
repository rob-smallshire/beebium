# The Music 5000

The Hybrid Music 5000 synthesiser, and its Music 3000 expander, as Beebium
builds them: a plugin on the 1 MHz bus whose sound joins the machine's audio
stream beside the SN76489's.

This is the component doc. The normative description of the real board is
`docs/music5000/hardware-model.md`, written from the original schematic and
the AM6070 datasheet; the reference material it rests on, and the survey of
existing emulators, are in `docs/music5000/`. The design is
`docs/discussion/music5000-design.md`. Companions: the extension framework
(`docs/peripheral-extension-framework.md`) for the bus claims and the audio
source port, and `docs/sound-subsystem.md` for the audio frames.

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

## What Beebium models

### The extension

`hybrid-music-5000` is a plugin attaching to `1mhz-bus`. Its one parameter,
`unit`, chooses the board:

| `unit` | JIM pages | Audio frame field |
|--------|-----------|-------------------|
| `music-5000` (default) | `&30`-`&3F` | 2 |
| `music-3000` | `&50`-`&5F` | 3 |

A machine with both lists the extension twice. Each instance claims its
pages through `OneMHzBusPort::claim_jim_pages()` (the port owns `&FCFF`, so
several JIM devices, and FRED devices such as the SCSI host adapter, share
the bus), takes host writes immediately, leaves reads floating, and is
reset by power-on and BREAK, which zero the normal register bank and the
bank-select flip-flop as the board's NRST line does.

The extension registers a bipolar 2 x int16 source on the machine's
`AudioSourcePort`, so `GetAudioFormat` describes its field as `Music5000`
(or `Music3000`), channels Left and Right, in its own group.

The pieces are separate, with no Beebium dependencies, so each is tested on
its own: `Music5000Synth` (the DSP), `Music5000OutputFilter` and
`Music5000Resampler`, all in `src/extensions/hybrid-music-5000/`.

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
| `model-b-music-5000` | Model B, 1770 DFS, Music 5000 |
| `model-b-music-5000-scsi` | The same with ADFS and the Acorn SCSI host adapter: two devices on the 1 MHz bus |

AMPLE, the software that drives the synthesiser, is the user's own sideways
ROM and discs, as for any add-on; no preset bundles it.

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

`GetAudioFormat` describes each field with its encoding and a `bipolar`
flag: the Music 5000's field is bipolar (signed, zero is silence), the
SN76489's are not. The Python client decodes fields generically. Playback
in the macOS client, which must honour the format rather than assume the
SN76489's layout, is issue #229.

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
