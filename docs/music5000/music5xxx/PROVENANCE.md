# music5xxx: a Music 5000 / Music 3000 clone PCB

## Provenance

- Source: https://github.com/jlprojects/music5xxx (the upstream README's
  image links point at the older name `jlbeebprojects/music5xxx`)
- Commit captured: `1a209d9e5b639dc1ba460d8cc6ae1592762a6347`
  (2019-08-13, the repository's only commit on master at fetch time)
- Fetched: 2026-10-26
- Author: J Lane (named in the schematic title block: "Clone of Hybrid
  Music 5000 by Chris Jordan", Rev 1, 2019-07-20, KiCad 5.1.2)
- Licence: Creative Commons Attribution-ShareAlike 4.0 International
  (CC BY-SA 4.0), per the upstream README's "Licence" section ("Unless
  otherwise marked, work in this repository is licenced under..."). The
  repository has no LICENSE file and GitHub reports no licence; the
  README statement is the grant. Files in this directory remain under
  CC BY-SA 4.0 and keep that licence if redistributed or adapted.

## What is here

| File | Upstream path | Notes |
|------|---------------|-------|
| `README-upstream.md` | `README.md` | Verbatim |
| `music5xxx-schematic.pdf` | `music5xxx Schematic.pdf` | 3 pages: top level (power, output filters, audio connectors), `Sheet1.sch` "1 MHz Bus", `Sheet2.sch` "Clock and DAC" |
| `music5xxx-bom.pdf`, `music5xxx-bom.ods` | `music5xxx BOM.pdf`, `.ods` | Verbatim |
| `music5xxx-bom.txt` | (derived) | `pdftotext -layout` of the BOM PDF |
| `kicad/*.sch`, `kicad/*.pro`, `kicad/*-lib-table` | `PCB/` | KiCad 5 legacy schematic sources: greppable netlists, the primary source for address decoding and wiring |
| `kicad/library/music5xxx-custom.{lib,dcm}` | `PCB/library/` | Custom symbols the schematic uses |

Not captured (listed for completeness): `PCB/music5xxx.kicad_pcb`
(layout), `PCB/gerber/`, `PCB/library/musix5xxx-custom.pretty/`
(footprints), `PCB/fp-info-cache`, `enclosure/` (3D-printed case STL and
Fusion 360 files) and `images/` (photographs of the built unit).
None bear on emulation.

## Changes from upstream

To keep the repository 7-bit ASCII:

- `kicad/Sheet1.sch`, `kicad/Sheet2.sch`: the global net label named by
  a capital O with stroke followed by 1 (the 1 MHz bus clock, "phi 1")
  is renamed `PHI1` throughout both sheets. The rename is consistent, so
  connectivity is unchanged. The PDF still shows the original glyph.
- `music5xxx-bom.txt`: en dashes replaced by hyphens, and the page-break
  form feeds from `pdftotext` removed.

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

Key parts: two AM9128-10 2Kx8 static RAMs (wave/register RAM and phase
RAM, IC22 and IC28), an AM6070 companding DAC (IC36), 74x283 adders,
74x163 counters and 74x374 latches.
