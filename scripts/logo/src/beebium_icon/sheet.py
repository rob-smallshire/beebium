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

"""A contact sheet for judging the level-of-detail ladder.

Renders every size at its native resolution on one background, then repeats
the row magnified with nearest-neighbour sampling, which is the only reliable
way to see what a 16px icon is actually doing to the pixel grid.
"""

from __future__ import annotations

import io

from PIL import Image, ImageDraw, ImageFont

from beebium_icon.config import IconConfig
from beebium_icon.raster import render_png

MARGIN = 32
GUTTER = 28
LABEL_HEIGHT = 22
ROW_GAP = 40


def build_sheet(
    config: IconConfig,
    sizes: tuple[int, ...],
    magnified_height: int = 160,
    background: str = "#F3EEE0",
) -> bytes:
    """Render a labelled contact sheet as PNG bytes."""
    sizes = tuple(sorted(set(sizes)))
    images = {size: _open(render_png(config, size)) for size in sizes}

    columns = [max(size, magnified_height) for size in sizes]
    width = MARGIN * 2 + sum(columns) + GUTTER * (len(sizes) - 1)
    native_height = max(sizes)
    height = (
        MARGIN * 2 + LABEL_HEIGHT + native_height + ROW_GAP + magnified_height
    )

    sheet = Image.new("RGBA", (width, height), background)
    draw = ImageDraw.Draw(sheet)
    font = _label_font()

    x = MARGIN
    native_baseline = MARGIN + LABEL_HEIGHT + native_height
    magnified_top = native_baseline + ROW_GAP
    for size, column in zip(sizes, columns):
        centre = x + column // 2
        draw.text(
            (centre, MARGIN), f"{size}px", fill="#6B6455", font=font, anchor="ma"
        )
        native = images[size]
        sheet.alpha_composite(
            native, (centre - size // 2, native_baseline - size)
        )
        magnified = native.resize(
            (magnified_height, magnified_height), Image.Resampling.NEAREST
        )
        sheet.alpha_composite(magnified, (centre - magnified_height // 2, magnified_top))
        x += column + GUTTER

    buffer = io.BytesIO()
    sheet.convert("RGB").save(buffer, format="PNG")
    return buffer.getvalue()


def _open(png: bytes) -> Image.Image:
    return Image.open(io.BytesIO(png)).convert("RGBA")


def _label_font() -> ImageFont.ImageFont:
    try:
        return ImageFont.load_default(size=16)
    except TypeError:  # Pillow older than 10.1 takes no size.
        return ImageFont.load_default()
