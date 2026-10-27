# Notes on music5xxx, the Music 5000 / Music 3000 clone PCB

The clone is not captured in this repository. These are our notes on it;
the files themselves are at the source below.

## Source

- Repository: https://github.com/jlprojects/music5xxx (the upstream
  README's image links point at the older name `jlbeebprojects/music5xxx`)
- Commit studied: `1a209d9e5b639dc1ba460d8cc6ae1592762a6347`
  (2019-08-13, the repository's only commit on master when read on
  2026-10-26)
- Author: J Lane (named in the schematic title block: "Clone of Hybrid
  Music 5000 by Chris Jordan", Rev 1, 2019-07-20, KiCad 5.1.2)
- Licence: Creative Commons Attribution-ShareAlike 4.0 International
  (CC BY-SA 4.0), per the upstream README's "Licence" section. The
  repository has no LICENSE file; the README statement is the grant.

## What is there

- `music5xxx Schematic.pdf`: 3 pages, top level (power, output filters,
  audio connectors), `Sheet1.sch` "1 MHz Bus", `Sheet2.sch` "Clock and
  DAC".
- `music5xxx BOM.pdf` and `.ods`.
- `PCB/`: KiCad 5 legacy schematic sources (`*.sch`, `*.pro`, the lib
  tables and `library/music5xxx-custom.{lib,dcm}`), the layout, gerbers
  and footprints. The `.sch` files are plain text and greppable, which
  made them a useful cross-check of address decoding and wiring while
  `hardware-model.md` was being derived from the original schematic.
- `enclosure/` (3D-printed case) and `images/` (photographs of the
  built unit).

One reading note: the 1 MHz bus clock net ("phi 1") is labelled with a
capital O with stroke followed by 1 in the sources and the PDF.

## How the clone differs from the original (per its README)

The clone was drawn from "the original schematic and photographs of the
original unit". Its README lists the deliberate departures: a different
power supply (15 V AC adapter, switching 5 V regulator), 74HCT logic in
place of 74LS, a 3.5 mm stereo jack in place of the DIN socket,
jumpers to act as a Music 3000 expander (a second 16-channel unit on
the same bus), and a gentler output low-pass filter "possibly at the
expense of unmasking some digital hash". The last one matters for
judging the sound of recordings made on a clone: an original unit's
reconstruction filter is more aggressive than this one.

There are two more departures that the README does not spell out. Both
come from the Stardot build thread (see `stardot-key-points.md`):

- The output filter's capacitors are 1n where the original has 3n3. The
  original's second stage cuts off near 3.2 kHz; the clone's cuts off
  near 16 kHz.
- The clock-input RC network (R37, C44, C45) misreads a factory mod. The
  original has 470R with 220pF to ground.

For the analogue output, page 3 of the original schematic
(`Hybrid Technology HT Music Synthesiser - Schematic.pdf`) is the
reference, not the clone.

Key parts: two AM9128-10 2Kx8 static RAMs (wave/register RAM and phase
RAM, IC22 and IC28), an AM6070 companding DAC (IC36), 74x283 adders,
74x163 counters and 74x374 latches.
