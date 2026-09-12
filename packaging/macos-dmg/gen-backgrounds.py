# Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
#
# This file is part of Beebium.
#
# Beebium is free software: you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version. Beebium is distributed in the hope that it will
# be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# You should have received a copy of the GNU General Public License along with Beebium.
# If not, see <https://www.gnu.org/licenses/>.

# /// script
# requires-python = ">=3.10"
# dependencies = ["pillow>=10"]
# ///
#
# Derive DMG background candidates from The Shape of Beebium artwork.
#
# The app icon IS the artwork, so a background made from the same image can
# swallow the icon. This produces THREE candidates for the maintainer to judge:
#   a  full-bleed, heavily dimmed + desaturated watermark of the Shape
#   b  a small Shape displaced to a quiet top band, well clear of both slots
#   c  no Shape at all: a subtle gradient sampled from the artwork's palette
# Each shares the same affordance: an arrow from the app slot to the
# Applications slot. There are no strips under the labels -- Finder's own label
# treatment carries them on the light ground (verified in light and dark).
#
# Every candidate is authored at 2x (1320x840) and 1x (660x420) and combined
# into a multi-resolution TIFF with `tiffutil -cathidpicheck`, so the Finder
# window is crisp on Retina. Layout constants mirror settings.py: 660x420 pt,
# 128 pt icons, slots at x=176 and x=484, baseline y=100.
#
# Usage:
#   uv run packaging/macos-dmg/gen-backgrounds.py [--art <png>] [--out <dir>]

import argparse
import os
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageEnhance, ImageFilter

SCALE = 2
W, H = 660 * SCALE, 420 * SCALE           # 1320 x 840, the 2x canvas
# Slots sit high (y=100 of 420 pt), so the icons land on the sky of the
# desaturated artwork and leave the geometric solids revealed below. The arrow
# (baked into the background) shares this baseline; keep it in step with
# settings.py's icon_locations.
APP_X, APPS_X, SLOT_Y = 176 * SCALE, 484 * SCALE, 100 * SCALE
ICON_HALF = 64 * SCALE                     # half of a 128 pt icon


def sample_palette(art):
    """Representative warm (sky) and cool (floor) colours from the artwork."""
    small = art.convert("RGB").resize((32, 32))
    top = small.crop((0, 4, 32, 12)).resize((1, 1)).getpixel((0, 0))       # sky
    bottom = small.crop((0, 22, 32, 32)).resize((1, 1)).getpixel((0, 0))   # floor
    return top, bottom


def vertical_gradient(size, top_rgb, bottom_rgb):
    w, h = size
    grad = Image.new("RGB", (1, h))
    for y in range(h):
        t = y / (h - 1)
        grad.putpixel((0, y), tuple(round(a + (b - a) * t) for a, b in zip(top_rgb, bottom_rgb)))
    return grad.resize((w, h))


def lighten(rgb, amount):
    """Mix a colour toward white by `amount` (0..1)."""
    return tuple(round(c + (255 - c) * amount) for c in rgb)


def cover_resize(img, size):
    """Resize+centre-crop `img` to exactly `size` (like CSS background cover)."""
    tw, th = size
    iw, ih = img.size
    scale = max(tw / iw, th / ih)
    resized = img.resize((round(iw * scale), round(ih * scale)), Image.LANCZOS)
    rw, rh = resized.size
    left, top = (rw - tw) // 2, (rh - th) // 2
    return resized.crop((left, top, left + tw, top + th))


# Bow height (sagitta, px at 2x) of the arc that carries the arrow. The arc is
# defined to pass through both icon bottom edges; a larger sagitta lifts the
# visible middle segment and steepens the tangents that aim into the icons.
ARROW_SAGITTA = 150


def draw_arrow(base, tint=(120, 122, 128), alpha=180):
    """A 'pick it up and move it across' arrow: a segment of ONE arc through
    both icon bottom edges.

    The underlying path is a circular arc, bowing upward, constrained to pass
    through the MIDPOINTS OF THE BOTTOM EDGES of the two icons
    (APP_X/APPS_X, SLOT_Y+ICON_HALF) -- not their centres. Geometrically an arc
    through the centres is "correct", but the eye judges a drag against the base
    of the object where it sits, so the centre arc reads as missing the icons;
    dropping the chord to the icon bottoms makes it read as passing through them.
    Only the middle segment is drawn -- trimmed to the same generous clearance
    (half-icon + 40 pt from each box) -- so the stroke floats in the gap while
    its ends, and the tangent-aligned arrowhead, visibly continue along the arc
    into each icon. Subdued translucent neutral grey with a rounded stroke and a
    clean arrowhead, in the macOS visual language; the app icon stays the focus.
    """
    overlay = Image.new("RGBA", base.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(overlay)
    color = tint + (alpha,)

    # Circular arc through the two icon bottom-edge midpoints (chord at
    # y=SLOT_Y+ICON_HALF from APP_X to APPS_X), bowing up by ARROW_SAGITTA. The
    # centre of the circle sits below the chord.
    chord_y = SLOT_Y + ICON_HALF
    mid_x = (APP_X + APPS_X) / 2.0
    half_chord = (APPS_X - APP_X) / 2.0
    sag = float(ARROW_SAGITTA)
    radius = (half_chord * half_chord + sag * sag) / (2.0 * sag)
    circ_y = chord_y - sag + radius

    def arc_y(x):
        return circ_y - (radius * radius - (x - mid_x) ** 2) ** 0.5

    def arc_slope(x):                                  # d(arc_y)/dx
        return (x - mid_x) / (radius * radius - (x - mid_x) ** 2) ** 0.5

    stroke = 7 * SCALE
    head_len = 22 * SCALE
    head_half = 14 * SCALE

    # Visible segment: from just outside the Beebium clearance to just before the
    # Applications clearance (the arrowhead completes the reach beyond the tip).
    x0 = APP_X + ICON_HALF + 40 * SCALE
    x_tip = APPS_X - ICON_HALF - 40 * SCALE
    steps = 64
    pts = [(x0 + (x_tip - x0) * i / steps, arc_y(x0 + (x_tip - x0) * i / steps))
           for i in range(steps + 1)]
    d.line(pts, fill=color, width=stroke, joint="curve")

    # Rounded start end (the tip end is finished by the arrowhead).
    r = stroke / 2.0
    sx, sy = pts[0]
    d.ellipse([sx - r, sy - r, sx + r, sy + r], fill=color)

    # Arrowhead tangent to the arc at the trim point, so its line extrapolates
    # along the arc into the Applications icon.
    tx, ty = pts[-1]
    m = arc_slope(x_tip)
    inv = 1.0 / (1.0 + m * m) ** 0.5
    ux, uy = inv, m * inv                              # unit tangent (+x)
    nx, ny = -uy, ux                                   # unit normal
    apex = (tx + ux * head_len, ty + uy * head_len)
    left = (tx + nx * head_half, ty + ny * head_half)
    right = (tx - nx * head_half, ty - ny * head_half)
    d.polygon([apex, left, right], fill=tint + (min(255, alpha + 20),))

    return Image.alpha_composite(base.convert("RGBA"), overlay)


def finish(base, out_dir, name):
    """Compose the arrow, write 1x/2x PNGs and a multi-rep TIFF.

    No boxes or strips under the labels: Finder draws the icon labels with its
    own contrast treatment, and the desaturated ground carries them. Legibility
    in light AND dark Finder is a property of the wash, tuned in the candidates.
    """
    img2x = draw_arrow(base).convert("RGB")
    p2x = out_dir / f"bg-{name}-2x.png"
    p1x = out_dir / f"bg-{name}-1x.png"
    img2x.save(p2x)
    img2x.resize((W // SCALE, H // SCALE), Image.LANCZOS).save(p1x)
    tiff = out_dir / f"bg-{name}.tiff"
    subprocess.run(
        ["tiffutil", "-cathidpicheck", str(p1x), str(p2x), "-out", str(tiff)],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    print(f"  {tiff}")
    return tiff


def candidate_a(art):
    """Full-bleed, heavily dimmed + desaturated watermark of the Shape."""
    cover = cover_resize(art.convert("RGB"), (W, H))
    cover = ImageEnhance.Color(cover).enhance(0.35)          # desaturate
    white = Image.new("RGB", (W, H), (255, 255, 255))
    return Image.blend(white, cover, 0.18)                   # 18% artwork


def candidate_b(art, top_rgb, bottom_rgb):
    """Small Shape in a quiet top band, clear of both slots, on a soft ground."""
    ground = vertical_gradient((W, H), lighten(top_rgb, 0.72), lighten(bottom_rgb, 0.82))
    shape = art.convert("RGB")
    shape = ImageEnhance.Color(shape).enhance(0.7)
    sw = 150 * SCALE
    shape = shape.resize((sw, sw), Image.LANCZOS)
    # Fade the tile's edges into the ground with a radial-ish alpha vignette.
    mask = Image.new("L", (sw, sw), 0)
    md = ImageDraw.Draw(mask)
    md.rounded_rectangle([0, 0, sw, sw], radius=18 * SCALE, fill=235)
    mask = mask.filter(ImageFilter.GaussianBlur(10 * SCALE))
    ground = ground.convert("RGBA")
    ground.paste(shape.convert("RGBA"), ((W - sw) // 2, 8 * SCALE), mask)
    return ground.convert("RGB")


def candidate_c(top_rgb, bottom_rgb):
    """No Shape: a subtle gradient sampled from the artwork palette."""
    return vertical_gradient((W, H), lighten(top_rgb, 0.66), lighten(bottom_rgb, 0.80))


def main():
    here = Path(__file__).resolve().parent
    repo = here.parent.parent
    ap = argparse.ArgumentParser()
    ap.add_argument("--art", default=str(repo / "scripts/logo/the-shape-of-beebium.png"))
    ap.add_argument("--out", default=str(here / "backgrounds"))
    args = ap.parse_args()

    art = Image.open(args.art)
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    top_rgb, bottom_rgb = sample_palette(art)
    print(f"palette: sky={top_rgb} floor={bottom_rgb}")

    print("candidates:")
    finish(candidate_a(art), out_dir, "a")
    finish(candidate_b(art, top_rgb, bottom_rgb), out_dir, "b")
    finish(candidate_c(top_rgb, bottom_rgb), out_dir, "c")


if __name__ == "__main__":
    if sys.platform != "darwin":
        sys.exit("this script needs macOS (tiffutil) to build the multi-rep TIFFs")
    main()
