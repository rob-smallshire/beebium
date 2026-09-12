# macOS DMG assembly

The Beebium macOS app ships as a per-architecture, drag-to-Applications DMG. The
Finder window layout is generated **headlessly** with
[dmgbuild](https://dmgbuild.readthedocs.io) (no Finder scripting, no `.DS_Store`
template to maintain), so the local preview loop and CI produce identical disk
images.

## Files

- `settings.py` — the dmgbuild settings: window geometry (660x420 pt), 128 pt
  icons, the two icon slots (app in the left third, `Applications` symlink in the
  right third, baseline `y=188`), and the background. The app path and background
  are passed as `-D` defines.
- `make-dmg.sh` — the single build entry point, used by both the local loop and
  CI. Runs dmgbuild via `uvx` against a **pinned** version.
- `gen-backgrounds.py` — derives background candidates from
  `scripts/logo/the-shape-of-beebium.png` (Pillow, run under `uv`). Emits the
  multi-resolution TIFFs the DMG needs.

Generated backgrounds and built `.dmg` files are **not** committed (see
`.gitignore`); the chosen background is committed once the maintainer picks it.

## Building a DMG

```bash
# Real release DMG (also how CI calls it):
packaging/macos-dmg/make-dmg.sh <Beebium.app> <version> <arch> <background.tiff>
#   -> Beebium-<version>-macos-<arch>.dmg   (in $OUT_DIR, default: cwd)

# Local preview: build with a distinct volume name and open it in Finder,
# so you can judge the look without CI.
packaging/macos-dmg/make-dmg.sh <Beebium.app> <version> <arch> <background.tiff> --preview
#   -> Beebium-preview-<bg>.dmg   (volume "Beebium preview <bg>")
```

## Background art constraints

The background is a **multi-resolution TIFF** so the window is crisp on Retina.
Author both sizes and combine them with `tiffutil`:

- **2x:** 1320 x 840 px (the design size; author here)
- **1x:** 660 x 840 -> 660 x 420 px (downscale of the 2x)
- combine: `tiffutil -cathidpicheck bg-1x.png bg-2x.png -out bg.tiff`

`gen-backgrounds.py` does all of this. When authoring art by hand:

- **One image serves both light and dark Finder** — there is no separate dark
  background, so keep it light enough that the system's dark icon-label text
  stays legible (each slot has a soft light strip under it for exactly this).
- **No version text** in the art — the volume name carries the version, and
  baking text in would force a re-author every release.
- **Keep the two icon slots quiet.** The app icon sits at `x=176` and the
  `Applications` symlink at `x=484` (pt), on the `y=188` baseline; leave those
  regions and the label strips just below them uncluttered so neither icon gets
  lost — the app icon is itself The Shape of Beebium, so a busy background made
  from the same artwork can swallow it.
- The centre, between the slots, carries a simple arrow (app -> Applications).

## The three candidates

`gen-backgrounds.py` produces three looks for the maintainer to choose between,
all sharing the arrow and the label strips:

- **a** — the Shape as a full-bleed, heavily dimmed and desaturated watermark.
- **b** — a small Shape displaced to a quiet top band, clear of both slots, on a
  soft ground.
- **c** — no Shape: a subtle gradient sampled from the artwork's palette.

## dmgbuild version

Pinned in `make-dmg.sh` (`DMGBUILD_VERSION`). Bump deliberately; a change there
changes every DMG the project ships.
