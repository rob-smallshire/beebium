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

"""Cutting the artwork to an icon."""

from __future__ import annotations

from dataclasses import replace

import pytest
from PIL import Image

from beebium_icon.artwork import (
    ArtworkError,
    body_box,
    load_artwork,
    render_image,
    wears_shadow,
)
from beebium_icon.config import ROUNDED, SQUIRCLE


def alpha(image: Image.Image, x: int, y: int) -> int:
    return image.getpixel((x, y))[3]


def test_the_artwork_is_square(config):
    source = load_artwork(config.artwork.filepath)
    assert source.width == source.height


def test_a_nonsquare_artwork_is_refused(config, tmp_path):
    filepath = tmp_path / "oblong.png"
    Image.new("RGB", (64, 32), "red").save(filepath)
    with pytest.raises(ArtworkError, match="must be square"):
        load_artwork(filepath)


def test_an_unreadable_artwork_is_refused(tmp_path):
    filepath = tmp_path / "not-an-image.png"
    filepath.write_bytes(b"certainly not a PNG")
    with pytest.raises(ArtworkError):
        load_artwork(filepath)


def test_a_nonpositive_size_is_rejected(config):
    with pytest.raises(ValueError, match="must be positive"):
        render_image(config, 0)


@pytest.mark.parametrize("size", [16, 32, 64, 128, 256, 512, 1024])
def test_the_icon_is_the_size_asked_for(config, size):
    assert render_image(config, size).size == (size, size)


@pytest.mark.parametrize("size", [16, 32, 64, 128, 256, 512, 1024])
def test_the_body_edges_fall_on_whole_pixels(config, size):
    body = body_box(config, size)
    assert body.x == int(body.x)
    assert body.width == int(body.width)


def test_the_body_is_centred_on_the_canvas(config):
    body = body_box(config, 512)
    assert body.x == pytest.approx(512 - body.right)
    assert body.y == pytest.approx(512 - body.bottom)


def test_the_corner_is_cut_away_and_the_centre_is_not(config):
    image = render_image(config, 512)
    assert alpha(image, 2, 2) == 0
    assert alpha(image, 256, 256) == 255


def test_the_rounded_shape_also_cuts_its_corner(config):
    image = render_image(config.with_shape(ROUNDED), 512)
    assert alpha(image, 1, 1) == 0
    assert alpha(image, 256, 256) == 255


def test_the_rounded_shape_reaches_the_canvas_edge(config):
    """It fills its canvas, unlike the squircle, which is inset for a shadow."""
    rounded = body_box(config.with_shape(ROUNDED), 512)
    squircled = body_box(config, 512)
    assert rounded.width == 512
    assert squircled.width < 512


def test_a_square_corner_ratio_of_zero_keeps_the_whole_canvas(config):
    plain = replace(
        config.with_shape(ROUNDED), rounded=replace(config.rounded, corner_ratio=0.0)
    )
    image = render_image(plain, 128)
    assert alpha(image, 0, 0) == 255


def test_the_shadow_appears_only_at_and_above_its_minimum_size(config):
    minimum = config.shadow.min_size
    assert not wears_shadow(config, minimum - 1)
    assert wears_shadow(config, minimum)


def test_the_rounded_shape_never_wears_a_shadow(config):
    """Its body fills the canvas, so there is nowhere to put one."""
    rounded = config.with_shape(ROUNDED)
    assert not wears_shadow(rounded, 1024)


def test_an_unshadowed_icon_gives_more_of_itself_to_the_artwork(config):
    """At 16px the margin Apple's grid reserves for the shadow is dead space."""
    small = body_box(config, 32).width / 32
    large = body_box(config, 512).width / 512
    assert small > large


def test_the_shadow_darkens_pixels_outside_the_body(config):
    # Hold the geometry still: dropping the shadow otherwise enlarges the
    # body, which would move the probe inside the artwork rather than beside
    # it, and the test would be measuring the wrong thing.
    fixed = replace(
        config,
        squircle=replace(config.squircle, unshadowed_body_ratio=config.squircle.body_ratio),
    )
    body = body_box(fixed, 512)
    # Just below the body, where a drop shadow with a positive dy must fall.
    below = (int(body.centre_x), int(body.bottom) + 4)
    assert render_image(fixed, 512).getpixel(below)[3] > 0

    unshadowed = replace(fixed, shadow=replace(fixed.shadow, opacity=0.0))
    assert render_image(unshadowed, 512).getpixel(below)[3] == 0


def test_sharpening_applies_only_to_a_large_reduction(config):
    """A 1024px icon is barely reduced; sharpening it would only add crunch."""
    source_width = load_artwork(config.artwork.filepath).width
    assert body_box(config, 1024).width / source_width >= config.artwork.sharpen_below_scale
    assert body_box(config, 64).width / source_width < config.artwork.sharpen_below_scale


def test_sharpening_changes_the_small_sizes(config):
    blunt = replace(config, artwork=replace(config.artwork, sharpen_percent=0))
    assert render_image(config, 32).tobytes() != render_image(blunt, 32).tobytes()


def test_every_size_is_resampled_from_the_source_not_from_a_reduction(config):
    """A 16px icon is not a shrunken 512px one."""
    small = render_image(config, 16)
    downsampled = render_image(config, 512).resize((16, 16), Image.Resampling.LANCZOS)
    assert small.tobytes() != downsampled.tobytes()


def test_the_icon_carries_the_artwork_not_a_flat_colour(config):
    colours = render_image(config, 128).convert("RGB").getcolors(maxcolors=1 << 16)
    assert colours is not None
    assert len(colours) > 500


def test_the_two_shapes_differ(config):
    assert (
        render_image(config, 256).tobytes()
        != render_image(config.with_shape(ROUNDED), 256).tobytes()
    )


def test_the_squircle_is_squarer_than_a_circular_corner(config):
    """The whole point of the superellipse: it fills more of the corner."""
    round_cornered = replace(config, squircle=replace(config.squircle, exponent=2.0))
    body = body_box(config, 512)
    probe = (int(body.x) + 12, int(body.y) + 12)
    assert render_image(config, 512).getpixel(probe)[3] > 0
    assert render_image(round_cornered, 512).getpixel(probe)[3] == 0


def test_the_shape_name_selects_the_geometry(config):
    assert config.shape == SQUIRCLE
    assert config.with_shape(ROUNDED).shape == ROUNDED
