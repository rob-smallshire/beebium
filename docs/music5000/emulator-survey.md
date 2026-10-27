# Music 5000 in existing emulators: a survey

Surveyed 2026-10-26 from the source trees on disk (commit noted per
tree) plus a fresh clone of hoglet's FPGA implementation. The purpose is
to find oracles and understand the existing models, and to know what is
derived from what before Beebium writes its own. Nothing here is a
design.

Beebium is GPL-3.0-or-later (README "License"), with GPL-3.0-only files
where derived from GPL-3.0-only sources. That sets which of the sources
below Beebium could take code from at all; the programme intends a fresh
model either way.

## Summary

| Implementation | Files | Licence | Lineage | Music 3000 | Output filter | Native rate, delivery |
|----------------|-------|---------|---------|-----------|---------------|-----------------------|
| B2 (`b2`, e1c9b84a) | none | - | - | - | - | No Music 5000 support |
| beebjit (`beebjit`, 523922c) | none | - | - | - | - | No Music 5000 support |
| B-Em (`b-em`, 0b6f1d2) | `src/music5000.c`, `.h`; hook in `src/6502.c` | GPL-2.0-or-later | Beech (Darren Izzard, 2015), ported by David Banks 2016, reworked 2017, Pi1MHz fixes back-ported 2020, filter 2023 | Yes (page nibble 5) | Yes, optional: 3rd-order Butterworth IIR at 3214 Hz, or a 16 kHz alternative | 46875 Hz, own Allegro voice; host resamples |
| Pi1MHz (`Pi1MHz`, 85c3a44) | `src/M5000_emulator.c`, `.h` | File header GPL-2.0-or-later (copied from B-Em); project GPL-3.0-or-later | Copy of B-Em's file (dp11, 2019), optimised; fixes flowed back to B-Em | Yes | No | 46875 Hz, Pi PWM audio |
| MAME (`mame`, a2502398c49) | `src/devices/bus/bbc/1mhzbus/m5000.cpp`, `.h` | BSD-3-Clause (header); "thanks-to: Darren Izzard" | Restructured from the B-Em/Pi1MHz model (Nigel Barnes, 2020); see the licence note below | Yes, plus Music 500 and Music 87 device names | No | clock/128 = 46875 Hz stream into MAME's mixer, which resamples |
| BeebEm Windows (`beebem-windows`, 592226e) | `Src/Music5000.cpp`, `.h`; hooks in `Src/BeebMem.cpp` | GPL-2.0-or-later | Mike Wyatt, January 2016; apparently independent of Beech (see below) | No | No | 46875 Hz, own SoundStreamer; host resamples |
| BeebEm Mac (`beebem-mac`, 012ef83) | `Src/Music5000.cpp`, `.h` | GPL-2.0-or-later | BeebEm Windows file plus `__APPLE__` guards on stream creation; model identical | No | No | as BeebEm |
| jsbeeb (`jsbeeb`, cdac20d) | `src/music5000.js`, `src/music5000-worklet.js`; hooks in `src/6502.js`, `src/web/audio-handler.js` | GPL-3.0-or-later | "Code ported from Beebem (C to .js) by Jason Robson": a translation of BeebEm | No | No | Separate `AudioContext({sampleRate: 46875})`; browser resamples |
| hoglet67/Music5000 FPGA (github, b0b495d, 2025-01-31; not on disk, cloned to scratch) | `src/Music5000.vhd`, `LogLinRom.vhd`, `iir_filter.vhd` | CC BY-NC-SA 4.0 (`LICENSE.txt`) | David Banks; a register-transfer reconstruction of the TTL schematic | Yes (`id` generic) | Yes, same Butterworth coefficients as B-Em | 46875 Hz, I2S/SPDIF/SPI DAC |

Not on disk and not surveyed: Beech itself (Darren Izzard's Windows
emulator, http://darrenizzard.com/index.php/2015/08/24/beech-a-bbc-micro-emulator/;
source availability not established) and the B-Em branch
`sf/m5000-filt` (merged into B-Em master as commits a2898e8 and ccc8365).

## Lineage and licence notes

- **Beech is the root of one family.** B-Em's file header reads
  "Copyright (C) 2016 Darren Izzard, based on the Music 5000 code from
  Beech". David Banks' first B-Em commit (52a4ff8, 2016-10-15) is
  "Ported Music 5000 code from Beech". His 2017 commits rewrote the
  core: stereo, no resampling, "one phase accumulator per channel pair",
  "use addition for amplitude, as working in log domain; add clamping on
  underflow". Those match his FPGA, which models the TTL directly, so
  the B-Em model since 2017 is better understood as a software
  rendering of the FPGA's behaviour than as Beech's.
- **Pi1MHz is a copy of B-Em's file.** Same header, same macros, same
  comments; dp11 later fixed bugs ("Fix Modulate bug when a channel
  becomes disabled") and Steve Fosdick back-ported them (B-Em 132118e,
  e032762, April 2020).
- **MAME's file is BSD-3-Clause but not independent.** It carries B-Em's
  comments word for word (the "c4d is used for 'Synchronization' e.g.
  the 'Wha' instrument" line and the whole log-domain amplitude block),
  the same `PanArray` values, and the same antilog formula. Its
  structure is new: a 6 MHz timer stepping the 3-bit program counter
  through the eight instruction slots. Whether its BSD label is sound
  given that text is MAME's concern; for us it means MAME is not an
  independent oracle for any behaviour it shares with B-Em.
- **BeebEm appears independent.** Mike Wyatt's file (January 2016)
  predates the B-Em port and shares only the names `c4d` and
  "modulate", plausibly from a common understanding of the schematic
  (or from Beech, which Pernod on Stardot says every Music 5000
  emulation descends from; this cannot be checked without Beech's
  source). Its model differs from the Beech family in the amplitude
  path, phase-set handling and readback (below).
- **jsbeeb is a translation of BeebEm** and inherits all of its model
  choices. A translation is a translation: had it been taken, it would
  carry BeebEm's GPL-2.0-or-later terms (jsbeeb is GPL-3.0-or-later).
- **The FPGA is CC BY-NC-SA 4.0.** NonCommercial and ShareAlike make it
  incompatible with GPL code; nothing may be derived from it. It is
  still the most valuable oracle for behaviour: it is a reconstruction
  of the TTL logic at register-transfer level, with comments explaining
  where it departs from the TTL (for instance, amplitude latching
  through the phase RAM). Read it to understand; do not transcribe it.
- **Filter coefficients.** B-Em's and the FPGA's IIR is the same
  Butterworth design (order 3, fs 46875, fc 3214 Hz) made with an online
  IIR tool from hoglet's estimate of the second stage's cut-off. It is
  a generic Butterworth, not a model derived from the component values,
  and the designer doubted on Stardot that a typical tap filter would do
  (see `stardot-key-points.md`).

## The common register model

All implementations agree on the host-visible map, and it is consistent
with the ETI article ("eight 256 byte pages in the 1MHz bus extended
address space"):

- `&FCFF` (FRED) is the JIM paging register. A Music 5000 responds when
  its upper nibble is 3 (`&3x`); a Music 3000 when it is 5 (`&5x`).
  Bits 3-1 select one of eight 256-byte blocks of the 2Kx8 wave RAM;
  bit 0 is ignored, so pages `&30`/`&31`, `&32`/`&33`... alias.
- `&FD00`-`&FDFF` (JIM) addresses the byte within that block.
- RAM `&000`-`&6FF` holds the 14 wave tables of 128 bytes; wave n is at
  `n * 128`. Samples are sign-magnitude log values: bit 7 set is
  positive, bits 6-0 a log magnitude.
- RAM `&700`-`&7FF` (pages `&3E`/`&3F`) holds two banks of channel
  registers, normal at `&700` and alternate (used when the previous
  channel modulates) at `&780`. In each bank, for channel c (0-15):
  `+&00+c` frequency low (bit 0 is the disable/phase-set bit),
  `+&10+c` frequency mid, `+&20+c` frequency high, `+&50+c` wave select
  (bits 7-4), `+&60+c` amplitude (log attenuation), `+&70+c` control
  (bit 5 modulate, bit 4 invert, bits 3-0 stereo position). `+&30` and
  `+&40` are unused.

All derive the sample rate the same way: 12 MHz crystal, 6 MHz
instruction rate, 8 instructions per channel, 16 channels, so
6e6 / 128 = 46875 Hz. (The ETI article's "1.2us" and "19.2us" do not fit
this; see the notes in `eti-inside-hybrids-music-5000.txt`.)

## Where the models disagree

These are the behaviours a Beebium model must settle from the schematic,
with the FPGA as the closest secondary source.

| Behaviour                   | B-Em / Pi1MHz                                                                | MAME                                                                 | BeebEm / jsbeeb                                                 | FPGA                                                                                           |
|-----------------------------|------------------------------------------------------------------------------|----------------------------------------------------------------------|-----------------------------------------------------------------|------------------------------------------------------------------------------------------------|
| Disable bit (freq lo bit 0) | Phase is loaded with the frequency (B-Em adds 0 + FREQ; Pi1MHz `PHASESET`)   | Phase forced to 0                                                    | Phase forced to 0                                               | Latched "phase set": adder's B input forced to 0, so phase := frequency                        |
| Frequency low byte          | All 8 bits added, including bit 0                                            | Masked with `&7E`: bit 7 dropped as well as bit 0 (looks like a bug) | Masked with `&FE`                                               | All bits through the adder                                                                     |
| Amplitude application       | 8-bit add in the log domain, clamp to zero on underflow (sign bit unchanged) | Same add and clamp                                                   | Linear: `data * min(amp, &80) / &80` before the antilog         | 8-bit add; gate on sign change                                                                 |
| Amplitude change timing     | Latched only at waveform wrap (carry out of the phase, `c4d`)                | Applied immediately                                                  | Applied immediately                                             | Latched at the zero crossing via the phase RAM                                                 |
| Antilog (AM6070)            | `2^C (2S + 33) - 33`, 13-bit, sign separate                                  | Same                                                                 | Chord/step table; algebraically the same curve                  | Same, in `LogLinRom`                                                                           |
| Stereo position             | Weighted sum: positions 0-7 all right; 8-10 all left; 11-15 left 5/6..1/6    | Same                                                                 | Same, as percentages                                            | Time-sliced chopping: position counter increments over the slots, as the ETI article describes |
| Channel timing              | All 16 channels computed at once per sample                                  | Per instruction slot on a 6 MHz timer                                | Per channel, 8 x 6 MHz cycles each, interleaved with CPU writes | Cycle-exact                                                                                    |
| Reads                       | Not decoded (write-only, as ETI says)                                        | Passed through                                                       | JIM and `&FCFF` reads return RAM / the page register            | `&FCFF` reads back the complement of the page register; JIM reads return RAM                   |
| Output filter               | Optional IIR (B-Em only)                                                     | None                                                                 | None                                                            | IIR                                                                                            |
| Gain/clipping               | Adaptive divisor on clip (B-Em); gain from cmdline (Pi1MHz)                  | Fixed scale                                                          | Clamp to 16 bits, scaled by global volume                       | Fixed-width sum                                                                                |

The reads row needs settling against the schematic: the ETI article
says the interface "is write-only", BeebEm, jsbeeb and the FPGA all
return data, and AMPLE or other software might probe for the hardware
by reading.

## Oracles available

- **Real-hardware recordings.** Coeus' Google Drive folder (24-bit
  96 kHz from a real Music 500, plus B-Em recordings of the same tunes),
  linked in `stardot-key-points.md`. Pilgrim Beart's "In
  Concert" on retro-kit was used to test a repaired unit.
- **FPGA (behaviour only).** Reconstructs the TTL; useful for
  questions like amplitude latching, phase-set, the stereo chopper and
  the register-address permutation the controller applies
  (`wave_addr` in `Music5000.vhd`).
- **B-Em with the filter on.** A convenient software comparison for
  any test tune, with the caveat that it shares its model with Pi1MHz
  and MAME, so agreement among those three is one vote, not three.
- **Schematics.** The original (`Hybrid Technology HT Music Synthesiser
  - Schematic.pdf`, here by hand) and the clone
  (https://github.com/jlprojects/music5xxx, CC BY-SA 4.0, not captured
  here; known departures in `music5xxx-notes.md`).
