# Music 500 / Music 5000

Reference material for emulating the Acorn Music 500 and the Hybrid
Technology Music 5000, the external digital synthesisers that hang off
the BBC Micro's 1 MHz bus (FRED/JIM, &FC00-&FDFF). The emulation is
planned as a peripheral extension, probably a plugin, attaching to the
`1mhz-bus` attachment point alongside the SCSI host adapter, which will
also exercise more than one device sharing the 1 MHz bus.

This directory holds the primary sources: manuals, circuit descriptions,
register maps and links, placed here by hand. Nothing in it is code.
Design notes go in `docs/discussion/` once the material has been read;
the built subsystem's documentation will live beside the other
component documents in `docs/`.

See also:

- `docs/peripheral-extension-framework.md` for the `1mhz-bus` attachment
  point, `OneMHzBusDevice` and the FRED/JIM offset convention.
- `docs/howto_write_a_peripheral_extension.md` for the plugin shape.
- `docs/clock-architecture.md` and `BusStretching.hpp` for 1 MHz bus
  stretching, which any device on this bus is subject to.
- `docs/sound-subsystem.md` for the audio pipeline the synthesiser's
  output has to be mixed into.

## Links

All fetched 2026-10-26 unless stated. Each captured file opens with its
own provenance note.

### Captured here

| File | Source | Author / publisher | Notes |
|------|--------|--------------------|-------|
| `eti-inside-hybrids-music-5000.txt`, `eti/` | http://www.colinfraser.com/m5000/m5-eti.htm | Chris Jordan, in ETI (c. 1986-87); transcribed to HTML by Colin Fraser | Architecture of the DSP. Text verbatim, figures transcribed by hand, plus notes on its inconsistent timing figures |
| `music5xxx-notes.md` | https://github.com/jlprojects/music5xxx (commit 1a209d9e) | (ours), on J Lane's 2019 clone PCB (CC BY-SA 4.0, not captured here) | What the clone repository holds and where it departs from the original |
| `stardot-key-points.md` | https://stardot.org.uk/forums/viewtopic.php?t=25687 and https://stardot.org.uk/forums/viewtopic.php?t=16895 | Threads opened by CHRISJJ (Chris Jordan), 2022-10-17, and jasonl (J Lane), 2019-04-10 | A summary, not a capture: the designer's points on emulation quality and the output filter, and the clone's departures, with post dates and links |
| `retro-kit-hybrid-music-5000.txt` | https://www.retro-kit.co.uk/page.cfm/content/Hybrid-Music-5000-Synthesiser/index.html | Paul Vernon, Retro Kit | Background on the Music 500/5000/87 variants; resource list |
| `emulator-survey.md` | local source trees and https://github.com/hoglet67/Music5000 | (ours) | Which emulators implement it, licences, lineage, where their models disagree |

Placed by hand, from the Retro Kit resource list:
`Hybrid_Music5000UG.pdf` (User Guide), `HybridMusic5000-UserGuide-Errata.pdf`,
`HybridMusic5000-UserGuide-Addendum.pdf` (dated 24 August 1989), and
`Hybrid Technology HT Music Synthesiser - Schematic.pdf` (the original
schematic; byte-identical to Retro Kit's copy).

### Listed, not captured

Copyrighted software, disc and ROM images, and documents with no
hardware content. Ask before capturing any of these.

- Colin Fraser's Hybrid page, http://www.colinfraser.com/m5000/m5000.htm:
  - AMPLE Nucleus Programmer's Guide (OCR'd, 208 pages, AMPLE language
    and software only; no hardware registers):
    http://www.colinfraser.com/m5000/ample-nucleus-pg.pdf
  - Music 2000 (MIDI interface) User Guide and a schematic of a
    functionally equivalent interface:
    http://www.colinfraser.com/m5000/m2000.pdf,
    http://www.colinfraser.com/m5000/m2000sch.pdf
  - Disc images (Music 5000 system disc with a patched AMPLE ROM image,
    Music 500 system disc, Amplinex disc magazine 1-16, AMPLE albums),
    under http://www.colinfraser.com/m5000/disks/ ; "provided for
    educational use only"
  - Recordings: m5-bach.mp3, m5-bachl.mp3, syncron.mp3 (oscillator sync)
- Retro Kit resources (all under
  https://www.retro-kit.co.uk/user/custom/Acorn/3rdParty/Hybrid/documents/):
  Music 5000 Installation Guide Iss. 2 (`System/Music-5000-Installation-Guide.pdf`),
  Universal programmers info (`scans/hybrid-universal-programmers.pdf`),
  the Hybrid Music System Manuals zip (`HybridMusicSystemManuals.zip`),
  "Random Access to the Time Domain in the AMPLE language"
  (`scans/random-access-to-the-time-domain-in-the-ample-language.pdf`),
  plus announcements, brochures, price lists and worksheets; the
  Retro Kit AMPLE disc archive at
  https://www.retro-kit.co.uk/page.cfm/content/Hybrid-Music-System-Music-Archive/index.html
- Reference recordings for listening comparison: Coeus' real Music 500
  recordings (24-bit 96 kHz) alongside B-Em renderings,
  https://drive.google.com/drive/folders/0B7B-wls7dWgXdGZhbWJRQ0tiRW8?resourcekey=0-HRc1RjWTwE0KEHmU36-xmA&usp=sharing ;
  "In Concert" by Pilgrim Beart,
  https://www.retro-kit.co.uk/user/custom/Acorn/3rdParty/Hybrid/audio/InConcert-PilgrimBeart.mp3
- Other implementations: hoglet's FPGA Music 5000,
  https://github.com/hoglet67/Music5000 (CC BY-NC-SA 4.0: read, never
  derive); Beech, the emulator the B-Em code came from,
  http://darrenizzard.com/index.php/2015/08/24/beech-a-bbc-micro-emulator/
- Chris Whytehead's Hybrid and Acorn Music 500 pages:
  http://chrisacorns.computinghistory.org.uk/8bit_Upgrades/Hybrid_Music5000.html,
  http://chrisacorns.computinghistory.org.uk/8bit_Upgrades/Acorn_ANV02_Music500.html
- Stardot threads not captured: dp11's Pi1MHz Music 5000
  (https://stardot.org.uk/forums/viewtopic.php?t=15848), hoglet's
  1 MHz bus FPGA (https://stardot.org.uk/forums/viewtopic.php?t=16643),
  the Pi1MHz optimisations back-ported to B-Em
  (https://stardot.org.uk/forums/viewtopic.php?f=2&t=19038), the AMPLE
  preservation thread (https://stardot.org.uk/forums/viewtopic.php?f=32&t=15850).
