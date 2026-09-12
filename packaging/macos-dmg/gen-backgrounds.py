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
# Each shares the same affordances: an arrow from the app slot to the
# Applications slot, and a light strip under each slot so the icon labels stay
# legible.
#
# Every candidate is authored at 2x (1320x840) and 1x (660x420) and combined
# into a multi-resolution TIFF with `tiffutil -cathidpicheck`, so the Finder
# window is crisp on Retina. Layout constants mirror settings.py: 660x420 pt,
# 128 pt icons, slots at x=176 and x=484, baseline y=188.
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
APP_X, APPS_X, SLOT_Y = 176 * SCALE, 484 * SCALE, 188 * SCALE
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


def draw_label_strips(base):
    """A soft, light rounded strip under each slot so labels read on any ground."""
    overlay = Image.new("RGBA", base.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(overlay)
    label_top = SLOT_Y + ICON_HALF + 10 * SCALE
    label_bot = label_top + 34 * SCALE
    for cx in (APP_X, APPS_X):
        half_w = 78 * SCALE
        d.rounded_rectangle(
            [cx - half_w, label_top, cx + half_w, label_bot],
            radius=10 * SCALE,
            fill=(255, 255, 255, 150),
        )
    overlay = overlay.filter(ImageFilter.GaussianBlur(2 * SCALE))
    return Image.alpha_composite(base.convert("RGBA"), overlay)


def draw_arrow(base, tint=(70, 82, 96)):
    """A simple arrow from the app slot toward the Applications slot."""
    overlay = Image.new("RGBA", base.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(overlay)
    x0 = APP_X + ICON_HALF + 26 * SCALE
    x1 = APPS_X - ICON_HALF - 26 * SCALE
    y = SLOT_Y
    shaft = 5 * SCALE
    d.line([(x0, y), (x1 - 14 * SCALE, y)], fill=tint + (210,), width=shaft)
    head = 15 * SCALE
    d.polygon(
        [(x1, y), (x1 - head, y - head), (x1 - head, y + head)],
        fill=tint + (230,),
    )
    return Image.alpha_composite(base.convert("RGBA"), overlay)


def finish(base, out_dir, name):
    """Compose label strips + arrow, write 1x/2x PNGs and a multi-rep TIFF."""
    img2x = draw_arrow(draw_label_strips(base)).convert("RGB")
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
