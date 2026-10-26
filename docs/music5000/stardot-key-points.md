# Stardot: key points on Music 5000 sound and the clone

A summary, not a capture. It covers two Stardot threads, read in full
on 2026-10-26 (logged out). Each point gives its post date (UTC) and a
link to the post. Posts belong to their authors; read them at the links.

- **t=25687** "Which BBC Micro emulator with Music 5000 support has the
  most faithful sound quality?",
  https://stardot.org.uk/forums/viewtopic.php?t=25687. Opened by
  CHRISJJ (Chris Jordan, designer of the Music 500/5000 and author of
  its User Guide) on 2022-10-17. 30 posts, the last on 2023-10-08.
- **t=16895** "Music 5000 clone in Kicad - Work in progress",
  https://stardot.org.uk/forums/viewtopic.php?t=16895. Opened by jasonl
  (J Lane, author of the clone in `music5xxx/`) on 2019-04-10. 116
  posts, the last on 2026-06-13.

Post links have the form
`https://stardot.org.uk/forums/viewtopic.php?p=NNNNNN#pNNNNNN`.

## What the designer says

Chris Jordan's technical points. An emulation has to answer to these.

1. **Existing emulation sounds poor.** 2022-10-17, p373095: the
   emulation in a YouTube recording
   (https://www.youtube.com/watch?v=QlajjjNvW3M; hoglet thought it was
   B-Em) sounded of noticeably poor quality to him.
2. **Resampling.** 2023-09-21, p403328: bad resampling would explain
   the poor quality he heard. Coeus had pointed out that the recording
   mixed the synthesiser with other sources, so it had been resampled.
   So the 46875 Hz native output has to reach the host rate through a
   good resampler, or not be resampled at all.
3. **The output filter is essential.** 2023-09-21, p403329: a PC sound
   card's reconstruction filter has very different characteristics, so
   it cannot stand in for the analogue output filter. Coeus (p373113)
   found no output filter in B-Em's emulation.
4. **DAC output stage.** 2023-09-21, p403364: he confirms that the 1K
   resistors at the filter input convert the AM6070's current output to
   a voltage.
5. **Model the analogue filter, not a short FIR.** 2023-09-22, p403423:
   Diminished's SPICE simulation of the original filter is a "design
   intent match". The simulation combines both op-amp stages and comes
   out at roughly 3rd order, about 18 dB/octave. A 15-tap FIR
   approximation was not close enough, and he doubts any typical tap
   (FIR) filter can suffice. p403498: an IIR is cheap in code, though he
   expects it to cost more CPU.
6. **What "cut-off" means here.** 2023-10-07, p404868: hoglet's "3214
   Hz" is the second stage's nominal cut-off and is easily misread; the
   response is not down 6 dB until nearly 6 kHz. Aliasing begins only
   above Nyquist, about 23 kHz. The filter is not an anti-aliasing
   filter, and an anti-aliasing filter on a DAC output is "a myth".
7. **The filter's purpose.** 2023-10-07, p404914: he designed it to
   improve the sound; its function is to reduce high frequencies.
   p404915: he would not call the stepped channel output quantisation
   noise, because the discrepancy is harmonic.
8. **The filter is deliberately severe.** 2020-09-14, t=16895 p288215:
   the filter is there to cut HF noise. He regretted it had to be that
   brutal, but judged that it did. Widening it, as the clone does,
   improves some instruments and tracks and degrades others. Hybrid
   developed and demonstrated through high-quality home audio
   equipment, with public demos through a Lecson AP1.
9. **Parts.** 2020-09-14, t=16895 p288216: as he recalls it, the
   original used no 74F parts. The 74F04 in the clone's BOM is the
   clone's choice.
10. **One design.** 2023-09-21, p403328: a Music 5000 converts to a Music
    3000 with a small track change, and a Music 500 to a 5000 easily.
11. **Keyboard software and the user port.** 2023-09-28, t=16895
    p404014: both the Music 5000 and a modded Music 500 work on a Model
    B. Neither touches the user port, but the Music 4000 keyboard
    software does, and it hung jsbeeb at that date.
12. **The FPGA version.** 2023-09-30, t=16895 p404242: having looked at
    hoglet's FPGA design, he thought its sound quality would be slightly
    improved. He does not say compared with what.

## Other facts from the threads

- **Shared code.** 2022-10-17, Pernod, p373103: every emulator with
  Music 5000 support descends from the same code, which came from the
  emulator Beech. (`emulator-survey.md` qualifies this for BeebEm.)
- **Sample rate and B-Em's filter.** 2023-09-22, Coeus, p403466: the
  sample rate is 46875 Hz. B-Em's branch `sf/m5000-filt`, since merged,
  adds an IIR output filter plus a 16 kHz alternative.
- **Pi1MHz fixes.** 2022-10-17, dp11, p373118, and Coeus, p373124:
  dp11 fixed bugs and cut CPU load while porting the emulation to
  Pi1MHz. The fixes were back-ported to B-Em in April 2020.
- **Hoglet's reading of the filter.** 2023-09-21, hoglet, p403349: the
  second op-amp stage is a 2nd-order low-pass at about 3214 Hz, and both
  channels are identical. His posted drawing of the original filter
  matches page 3 of the original schematic in this directory (15K
  resistors, 3n3 capacitors, TL072).
- **Real-hardware recordings.** 2022-10-17, Coeus, p373109: recordings
  of a real Music 500 (24-bit, 96 kHz) and B-Em renderings of the same
  tunes are in a Google Drive folder:
  https://drive.google.com/drive/folders/0B7B-wls7dWgXdGZhbWJRQ0tiRW8?resourcekey=0-HRc1RjWTwE0KEHmU36-xmA&usp=sharing
- **The clone's filter.** 2019-04-27, jasonl, t=16895 p235383: the
  original filter (15K, 3n3) cuts off near 3.2 kHz. He changed the
  capacitors to 1n for a cut-off near 16 kHz, which is the
  `music5xxx/` schematic as published.
- **The clone's clock-input network.** 2020-08-07, eelco108, t=16895
  p282670 and p282674, confirmed in p330481: the clone's R37 (4K7 in
  series with the 1 MHz bus clock feeding E) misreads a factory mod.
  The original has 470R with 220pF to ground, an RC low-pass near
  1.5 MHz. The clone's C45 should be 220pF, and C44 belongs on IC31A
  pin 7.
- **Op-amps.** 2026-06-13, maniacminer, t=16895 p486314: some Music 500
  units have 741 op-amps in the output stage. He found faster modern
  op-amps sounded harsher (he puts it down to slew rate). The clone
  uses TL072s.
