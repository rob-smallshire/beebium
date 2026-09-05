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

"""Cutting the source artwork to an icon at one size.

The artwork is a finished square image - "The Shape of Beebium", after Acorn's
"The Shape of Things to Come" - so making an icon of it is a matter of
resampling it well and cutting it to the shape the platform expects, rather
than of drawing anything.

The mask is rasterised from an SVG path by resvg, which antialiases the corner
properly at every size; cutting the corner with a polygon fill would show its
stairs at 16px.
"""

from __future__ import annotations

import functools
import io

import resvg_py
from PIL import Image, ImageFilter

from beebium_icon.config import ROUNDED, SQUIRCLE, IconConfig
from beebium_icon.geometry import Box, rounded_rect_path, square, squircle_path

SVG_NAMESPACE = "http://www.w3.org/2000/svg"


class ArtworkError(ValueError):
    """Raised when the source artwork cannot be used as an icon."""


@functools.lru_cache(maxsize=4)
def load_artwork(filepath) -> Image.Image:
    """Load the source image, which must be square."""
    try:
        image = Image.open(filepath)
        image.load()
    except OSError as error:
        raise ArtworkError(f"{filepath}: {error}") from error
    if image.width != image.height:
        raise ArtworkError(
            f"{filepath}: the artwork must be square, but it is "
            f"{image.width}x{image.height}"
        )
    return image.convert("RGB")


def wears_shadow(config: IconConfig, size: int) -> bool:
    """Whether an icon of this size and shape is drawn with a shadow."""
    return (
        config.shape == SQUIRCLE
        and size >= config.shadow.min_size
        and config.shadow.opacity > 0
    )


def body_box(config: IconConfig, size: int) -> Box:
    """Where the artwork sits on the canvas."""
    if config.shape == SQUIRCLE:
        ratio = (
            config.squircle.body_ratio
            if wears_shadow(config, size)
            else config.squircle.unshadowed_body_ratio
        )
    else:
        ratio = config.rounded.body_ratio
    # An even margin puts the body edges on whole pixels, which matters at
    # 16px where a half-pixel edge is a grey smear round the whole icon.
    side = round(size * ratio)
    if (size - side) % 2:
        side -= 1
    return square(max(side, 1), size)


def shape_path(config: IconConfig, body: Box) -> str:
    """SVG path data for the outline the artwork is cut to."""
    if config.shape == SQUIRCLE:
        return squircle_path(
            body, body.width * config.squircle.corner_ratio, config.squircle.exponent
        )
    return rounded_rect_path(body, body.width * config.rounded.corner_ratio)


def render_image(config: IconConfig, size: int) -> Image.Image:
    """Render the icon at one size as an RGBA image."""
    if size <= 0:
        raise ValueError(f"icon size must be positive, got {size}")
    body = body_box(config, size)
    path = shape_path(config, body)
    mask = _rasterise(_svg(size, f'<path d="{path}" fill="#fff"/>')).split()[3]

    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    if wears_shadow(config, size):
        canvas.alpha_composite(_shadow_layer(config, size, path, mask))

    side = int(body.width)
    cut = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    cut.paste(_resampled(config, side).convert("RGBA"), (int(body.x), int(body.y)))
    cut.putalpha(mask)
    canvas.alpha_composite(cut)
    return canvas


def render_png(config: IconConfig, size: int) -> bytes:
    """Render the icon at one size as PNG bytes."""
    buffer = io.BytesIO()
    render_image(config, size).save(buffer, format="PNG", optimize=True)
    return buffer.getvalue()


def _resampled(config: IconConfig, side: int) -> Image.Image:
    source = load_artwork(config.artwork.filepath)
    image = source.resize((side, side), Image.Resampling.LANCZOS)
    scale = side / source.width
    if scale < config.artwork.sharpen_below_scale and config.artwork.sharpen_percent:
        # A large reduction throws away the horizon and the cylinder's edge;
        # unsharp masking puts back the definition Lanczos averages away.
        image = image.filter(
            ImageFilter.UnsharpMask(
                radius=max(0.6, side / 64),
                percent=config.artwork.sharpen_percent,
                threshold=0,
            )
        )
    return image


def _shadow_layer(
    config: IconConfig, size: int, path: str, mask: Image.Image
) -> Image.Image:
    shadow = config.shadow
    layer = _rasterise(
        _svg(
            size,
            f'<defs><filter id="s" x="-25%" y="-25%" width="150%" height="150%">'
            f'<feDropShadow dx="0" dy="{size * shadow.dy:.3f}" '
            f'stdDeviation="{size * shadow.blur:.3f}" '
            f'flood-color="{shadow.colour}" flood-opacity="{shadow.opacity}"/>'
            f'</filter></defs><path d="{path}" fill="#000" filter="url(#s)"/>',
        )
    )
    # Keep only what falls outside the body: the shape itself is about to be
    # covered by the artwork, and letting it show through would darken a
    # translucent edge pixel twice.
    alpha = layer.split()[3]
    outside = mask.point(lambda value: 255 - value)
    layer.putalpha(Image.composite(alpha, Image.new("L", alpha.size, 0), outside))
    return layer


def _svg(size: int, body: str) -> str:
    return (
        f'<svg xmlns="{SVG_NAMESPACE}" width="{size}" height="{size}" '
        f'viewBox="0 0 {size} {size}">{body}</svg>'
    )


def _rasterise(svg: str) -> Image.Image:
    return Image.open(io.BytesIO(bytes(resvg_py.svg_to_bytes(svg_string=svg)))).convert(
        "RGBA"
    )
