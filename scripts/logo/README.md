# Beebium icon packager

The Beebium application icon is **The Shape of Beebium**: a pastiche of
Acorn's 1981 marketing image *The Shape of Things to Come*, which posed a BBC
Micro on a cylindrical plinth before a sunset over a mirrored, strongly
converging floor. The pastiche keeps the sky, the floor and the three solids -
cylinder, cuboid, pyramid - and omits the computer.

The artwork is finished, so this tool does not draw anything. It cuts one
square image to the sizes, shapes and container formats that each platform
asks for.

```
the-shape-of-beebium.png  ->  .icns + AppIcon.appiconset   (macOS)
                              .ico + PNGs                  (Windows)
                              hicolor tree                 (Linux)
                              favicon, touch icon, PNGs    (web)
```

## Using it

The tool is a uv project; `uv run` will create the environment on first use.

```bash
cd scripts/logo

uv run beebium-icon build                       # every platform, into out/
uv run beebium-icon build macos windows         # named platforms only
uv run beebium-icon sheet -o /tmp/ladder.png    # contact sheet for the small sizes
uv run beebium-icon png --size 1024 -o /tmp/icon.png
uv run pytest                                   # the test suite
```

To put the generated icons into the macOS front end:

```bash
uv run beebium-icon build macos --install-appiconset \
    ../../clients/macos/Beebium/Beebium/Assets.xcassets/AppIcon.appiconset
```

To regenerate the logo the top-level README shows:

```bash
uv run beebium-icon --shape rounded png --size 512 \
    -o ../../docs/images/beebium-logo.png
```

## Shapes

Two, selected per target and overridable with `--shape`:

- **`squircle`** - Apple's icon grid: a superellipse body inset to 0.805 of the
  canvas, leaving the margin macOS expects a drop shadow to occupy. Used for
  the `macos` target.
- **`rounded`** - a conventional rounded square filling the canvas, with no
  shadow, which is what Windows and Linux expect. A `corner_ratio` of 0 makes
  it a plain square. Used everywhere else.

## What the small sizes need

The artwork reads at 16px because of its palette - orange sky, white sun, blue
floor - long after the solids have dissolved. Two things protect that:

- **No shadow below 64px.** The shadow is a property of Apple's grid, and the
  margin it reserves is 20% of the canvas. Below 64px the icon gives that
  margin back to the artwork instead, growing the body from 0.805 to 0.94.
- **Sharpening below half size.** Lanczos averages away the horizon and the
  cylinder's lit edge. Unsharp masking puts them back. Above half size the
  reduction is small enough that sharpening only roughens the clouds, so it is
  not applied.

`beebium-icon sheet` renders every size at its native resolution and again
magnified with nearest-neighbour sampling, which is the only reliable way to
see what a 16px icon is doing to the pixel grid.

## Configuration

Everything comes from a TOML file - `configs/beebium.toml` by default: the
artwork's path, the resampling thresholds, the proportions of both shapes, and
the shadow. Every key has a default in `src/beebium_icon/config.py`, which is
the reference for what each one means. Unknown keys are rejected rather than
ignored.

## How it fits together

| Module        | Responsibility                                          |
|---------------|---------------------------------------------------------|
| `config.py`   | the TOML schema, its defaults and its validation         |
| `geometry.py` | boxes, rounded rectangles, superellipses                 |
| `artwork.py`  | resampling, sharpening, masking, the shadow              |
| `raster.py`   | the ICNS and ICO containers                              |
| `bundle.py`   | per-platform file layouts                                |
| `sheet.py`    | the contact sheet                                        |
| `cli.py`      | the command line                                         |

The mask is rasterised from an SVG path by resvg, which ships as a wheel and
needs no system Cairo, so `uv run` is the only prerequisite on any platform.
Cutting the corner with a polygon fill instead would show its stairs at 16px.

The ICNS archive is assembled here rather than by `iconutil`, which is
macOS-only. 16 and 32 travel as `icp4` and `icp5`; there is deliberately no
`icp6`, because macOS reads that type back as 48x48.
