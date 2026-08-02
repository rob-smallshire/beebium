# Beebium icon generator

Generates the Beebium application icon: a periodic-table tile for the fictional
element **Beebium**, symbol **Bb**, atomic number 6502, in BBC Microcomputer
beige and Acorn green.

The tile uses a level-of-detail scheme.  At 16px it is the symbol alone; each
step up the ladder adds an annotation, until at 512px and above the tile
carries every slot: the atomic number, the ground state, the element name,
the isotopes and the energy levels.  Every size is composed at that
size - nothing is downsampled from one large drawing - so the small end is a
deliberate design rather than a blur.

```
16, 32          Bb
64              Bb, keyline
128             + 6502, Beebium
256             + isotopes: 16Bb . 32Bb . 64Bb . 128Bb
512             + 8-bit, energy levels: 2 . 3 . 4 MHz
1024            + constituents: 6502 . 6522 . 6845 . 6850 . 6854
```

What each slot means, in the manner of the legend on a NIST periodic table:

| Slot           | On the tile               | For a BBC Micro                        |
|----------------|---------------------------|----------------------------------------|
| Atomic number  | `6502`                    | the processor at the heart of every one |
| Ground state   | `8-bit`                   | the word size, which stays true across the isotopes |
| Symbol         | `Bb`                      | -                                      |
| Name           | `Beebium`                 | -                                      |
| Isotopes       | `16Bb . 32Bb . 64Bb`      | the RAM the machine came with, as mass numbers |
| Energy levels  | `2 . 3 . 4 MHz`           | the clock speeds it runs at            |
| Constituents   | `6502 . 6522 . 6845 ...`  | the chips it is built from             |

## Using it

The tool is a uv project; `uv run` will create the environment on first use.

```bash
cd scripts/logo

uv run beebium-icon build                       # every platform, into out/
uv run beebium-icon build macos windows         # named platforms only
uv run beebium-icon sheet -o /tmp/ladder.png    # contact sheet of the ladder
uv run beebium-icon svg --size 1024 -o /tmp/icon.svg
uv run beebium-icon png --size 256 -o /tmp/icon.png
uv run pytest                                   # the test suite
```

`build` writes, beneath `out/`:

| Target    | Contents                                                          |
|-----------|-------------------------------------------------------------------|
| `macos`   | `Beebium.icns` and an `AppIcon.appiconset` with its `Contents.json` |
| `windows` | multi-resolution `beebium.ico` (PNG-compressed entries) plus PNGs  |
| `linux`   | a `hicolor` theme tree, including `scalable/apps/beebium.svg`      |
| `web`     | `favicon.ico`, `apple-touch-icon.png`, PNGs and a scalable SVG      |

To put the generated icons into the macOS front end:

```bash
uv run beebium-icon build macos --install-appiconset \
    ../../clients/macos/Beebium/Beebium/Assets.xcassets/AppIcon.appiconset
```

## Configuration

Everything drawn comes from a TOML file - `configs/beebium.toml` by default.
A machine variant, an isotope for a second processor, is a new configuration
file rather than new code:

```bash
uv run beebium-icon -c configs/beebium-z80.toml build macos
```

The sections are `palette`, `geometry`, `layout`, `content`, `lod` and `fonts`;
every key has a default in `src/beebium_icon/config.py`, which is the reference
for what each one means.  Unknown keys are rejected rather than ignored.

`lod` holds the smallest rendered size at which each element appears, so the
ladder above is data, not code.

## Notation

Content strings carry a small markup for raised and lowered text, because
chemistry is written that way:

```
2^4K            a raised 4
^{16}Bb         a raised 16, as a mass number
(6522)_2        a lowered 2, as in a molecular formula
\^              a literal caret
```

Scripts are drawn by scaling and shifting the same face, not by asking for
Unicode superscript characters, which most faces carry for only a few digits,
nor for OpenType script features, which many faces lack.  Their size and
offsets are the `[scripts]` section.

The isotope row has two styles, since it is generated rather than written out:
`nuclide` sets each RAM size as a raised mass number before the symbol, as
chemistry writes an isotope; `unit` sets `16K . 32K` instead.  To put the
constituents in place of the energy levels rather than below them, empty
`content.energies` and lower `lod.constituents` to 512.

## Frame styles

Two outer shapes, selected per target and overridable with `--style`:

- **`squircle`** - a superellipse-cornered body on Apple's icon grid, which is
  what a macOS app icon should be.  Used for the `macos` target.
- **`tile`** - the free-standing tile as drawn in the original concept: a
  lighter beige band around a beige face.  Used for the other targets, and
  portable anywhere.

## Typography

Two typefaces, because a periodic table separates the identity of an element
from its measurements:

| Role        | Carries                                     |
|-------------|---------------------------------------------|
| `symbol`    | `Bb`                                        |
| `name`      | `Beebium`                                   |
| `technical` | 6502, 8-bit, isotopes, energy levels        |

Text is shaped with HarfBuzz and converted to outlines when the SVG is written,
so a generated icon depends on no installed font and comes out identically on
any machine.  A role may name a `.ttc` face index and variable-font axis
settings; `small_variations` gives a role a lighter cut at and below
`lod.small_size`, because a heavy weight fills in its own counters once a glyph
is eight pixels tall.

`configs/beebium.toml` uses the open-licensed faces vendored in `fonts/`, and is
the configuration the shipped icons are built from.  `configs/beebium-avenir-din.toml`
uses Avenir Next and DIN Alternate from `/System/Library/Fonts`, which are the
faces the design was drawn for; it renders only on macOS and its output must
not be checked in, since those fonts are not redistributable.

## How it fits together

| Module          | Responsibility                                              |
|-----------------|-------------------------------------------------------------|
| `config.py`     | the TOML schema, its defaults and its validation             |
| `geometry.py`   | boxes, rounded rectangles, superellipses                     |
| `typography.py` | shaping and outlining text                                   |
| `layout.py`     | level of detail, and where each block sits                   |
| `svg.py`        | the SVG document                                             |
| `raster.py`     | rasterising, and the ICNS and ICO containers                 |
| `bundle.py`     | per-platform file layouts                                    |
| `sheet.py`      | the contact sheet                                            |
| `cli.py`        | the command line                                             |

Rasterising uses resvg, which ships as a wheel and needs no system Cairo, so
`uv run` is the only prerequisite on any platform.

The ICNS archive is assembled here rather than by `iconutil`, which is
macOS-only.  16 and 32 travel as `icp4` and `icp5`; there is deliberately no
`icp6`, because macOS reads that type back as 48x48.
