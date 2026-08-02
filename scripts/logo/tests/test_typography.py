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

"""Shaping, measuring and outlining text."""

from __future__ import annotations

from dataclasses import replace

import pytest

from beebium_icon.config import FIT_CAP, FIT_INK, NAME, SYMBOL, TECHNICAL
from beebium_icon.geometry import Box
from beebium_icon.typography import TypographyError, face_for

from tests_support import path_bounds


@pytest.fixture(scope="module")
def symbol_face(config):
    return face_for(config.fonts[SYMBOL])


def test_shaping_produces_one_glyph_per_character(symbol_face):
    assert len(symbol_face.shape("Bb").glyphs) == 2


def test_a_run_has_positive_ink(symbol_face):
    ink = symbol_face.shape("Bb").ink
    assert ink is not None
    assert ink.width > 0
    assert ink.height > 0


def test_ink_of_a_capital_matches_the_cap_height(symbol_face):
    assert symbol_face.shape("H").ink.height == pytest.approx(symbol_face.cap_height)


def test_a_longer_string_advances_further(symbol_face):
    assert symbol_face.shape("Bbb").advance > symbol_face.shape("Bb").advance


def test_tracking_widens_a_run_but_not_a_single_glyph(config):
    plain = face_for(replace(config.fonts[NAME], tracking=0.0))
    tracked = face_for(replace(config.fonts[NAME], tracking=0.1))
    assert tracked.shape("Beebium").advance > plain.shape("Beebium").advance
    assert tracked.shape("B").advance == pytest.approx(plain.shape("B").advance)


def test_a_missing_glyph_names_the_character(config):
    face = face_for(config.fonts[TECHNICAL])
    with pytest.raises(TypographyError, match=r"U\+2603"):
        face.shape("snow ☃ man")


def test_the_separator_is_available_in_the_technical_face(config):
    face = face_for(config.fonts[TECHNICAL])
    assert face.shape(config.content.separator.strip()).glyphs


@pytest.mark.parametrize("align", ["left", "centre", "right"])
def test_a_run_is_fitted_inside_its_box(symbol_face, align):
    box = Box(100, 200, 300, 120)
    bounds = path_bounds(symbol_face.shape("Bb").path_data(box, align))
    assert box.x - 0.5 <= bounds[0] and bounds[2] <= box.right + 0.5
    assert box.y - 0.5 <= bounds[1] and bounds[3] <= box.bottom + 0.5


def test_alignment_places_the_ink_against_the_named_edge(symbol_face):
    box = Box(0, 0, 400, 100)
    run = symbol_face.shape("Bb")
    left = path_bounds(run.path_data(box, "left"))
    centre = path_bounds(run.path_data(box, "centre"))
    right = path_bounds(run.path_data(box, "right"))
    assert left[0] == pytest.approx(box.x, abs=0.5)
    assert right[2] == pytest.approx(box.right, abs=0.5)
    assert (centre[0] + centre[2]) / 2 == pytest.approx(box.centre_x, abs=0.5)


def test_an_unknown_alignment_is_rejected(symbol_face):
    with pytest.raises(TypographyError, match="alignment"):
        symbol_face.shape("Bb").path_data(Box(0, 0, 10, 10), "middling")


def test_ink_fit_fills_the_box_height(config):
    face = face_for(replace(config.fonts[SYMBOL], fit=FIT_INK))
    box = Box(0, 0, 1000, 100)
    bounds = path_bounds(face.shape("Bb").path_data(box, "centre"))
    assert bounds[3] - bounds[1] == pytest.approx(box.height, abs=0.5)


def test_cap_fit_sizes_by_cap_height_so_that_rows_match(config):
    """Runs that differ only in descenders or dots must come out the same size."""
    face = face_for(replace(config.fonts[TECHNICAL], fit=FIT_CAP))
    box = Box(0, 0, 10_000, 100)
    with_dots = path_bounds(face.shape("16 · 32").path_data(box, "centre"))
    without = path_bounds(face.shape("1632").path_data(box, "centre"))
    assert with_dots[3] - with_dots[1] == pytest.approx(without[3] - without[1], abs=0.5)


def test_a_wide_run_is_scaled_down_to_fit_the_width(symbol_face):
    narrow = Box(0, 0, 40, 400)
    bounds = path_bounds(symbol_face.shape("Bb").path_data(narrow, "centre"))
    assert bounds[2] - bounds[0] == pytest.approx(narrow.width, abs=0.5)
    assert bounds[3] - bounds[1] < narrow.height


def test_a_run_of_only_spaces_has_no_ink_and_draws_nothing(config):
    face = face_for(config.fonts[NAME])
    run = face.shape("   ")
    assert run.ink is None
    assert run.path_data(Box(0, 0, 100, 100)) == ""


def test_faces_are_cached_per_role(config):
    assert face_for(config.fonts[SYMBOL]) is face_for(config.fonts[SYMBOL])


def test_weight_changes_the_outline(config):
    light = face_for(replace(config.fonts[SYMBOL], variations={"wght": 300}))
    heavy = face_for(replace(config.fonts[SYMBOL], variations={"wght": 1000}))
    box = Box(0, 0, 10_000, 100)
    light_bounds = path_bounds(light.shape("Bb").path_data(box, "left"))
    heavy_bounds = path_bounds(heavy.shape("Bb").path_data(box, "left"))
    assert heavy_bounds[2] - heavy_bounds[0] > light_bounds[2] - light_bounds[0]
