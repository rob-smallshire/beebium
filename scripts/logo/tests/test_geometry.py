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

"""Boxes and outlines."""

from __future__ import annotations

import pytest

from beebium_icon.geometry import Box, rounded_rect_path, square, squircle_path

from tests_support import outline_points as points


def test_box_edges():
    box = Box(10, 20, 100, 200)
    assert box.right == 110
    assert box.bottom == 220
    assert box.centre_x == 60
    assert box.centre_y == 120


def test_inset_shrinks_on_every_side():
    box = Box(0, 0, 100, 100).inset(10)
    assert (box.x, box.y, box.width, box.height) == (10, 10, 80, 80)


def test_slice_top_partitions_the_box():
    top, rest = Box(0, 0, 100, 100).slice_top(30)
    assert (top.y, top.height) == (0, 30)
    assert (rest.y, rest.height) == (30, 70)


def test_slice_top_cannot_take_more_than_there_is():
    top, rest = Box(0, 0, 100, 100).slice_top(150)
    assert top.height == 100
    assert rest.height == 0


def test_square_is_centred():
    box = square(80, 100)
    assert (box.x, box.y) == (10, 10)
    assert box.width == box.height == 80


@pytest.mark.parametrize("radius", [0, 10, 40, 100])
def test_rounded_rect_stays_inside_its_box(radius):
    box = Box(5, 7, 80, 80)
    for x, y in points(rounded_rect_path(box, radius)):
        assert box.x - 0.01 <= x <= box.right + 0.01
        assert box.y - 0.01 <= y <= box.bottom + 0.01


def test_rounded_rect_radius_is_clamped_to_half_the_side():
    box = Box(0, 0, 40, 40)
    assert rounded_rect_path(box, 40) == rounded_rect_path(box, 20)


def test_paths_are_closed():
    box = Box(0, 0, 100, 100)
    assert rounded_rect_path(box, 20).endswith("Z")
    assert squircle_path(box, 20, 5.0).endswith("Z")


@pytest.mark.parametrize("exponent", [2.0, 3.0, 5.0, 8.0])
def test_squircle_stays_inside_its_box(exponent):
    box = Box(3, 4, 120, 120)
    for x, y in points(squircle_path(box, 30, exponent)):
        assert box.x - 0.5 <= x <= box.right + 0.5
        assert box.y - 0.5 <= y <= box.bottom + 0.5


def test_squircle_touches_every_edge():
    box = Box(0, 0, 100, 100)
    xs = [x for x, _ in points(squircle_path(box, 25, 5.0))]
    ys = [y for _, y in points(squircle_path(box, 25, 5.0))]
    assert min(xs) == pytest.approx(0, abs=0.5)
    assert max(xs) == pytest.approx(100, abs=0.5)
    assert min(ys) == pytest.approx(0, abs=0.5)
    assert max(ys) == pytest.approx(100, abs=0.5)


def test_a_squarer_exponent_fills_more_of_the_corner():
    """The whole point of the superellipse: exponent controls corner fullness."""
    box = Box(0, 0, 100, 100)

    def corner_distance(exponent: float) -> float:
        # How near the outline comes to the box corner at (0, 0).
        return min(x * x + y * y for x, y in points(squircle_path(box, 50, exponent)))

    assert corner_distance(8.0) < corner_distance(5.0) < corner_distance(2.0)


def test_zero_radius_squircle_is_a_rectangle():
    box = Box(0, 0, 10, 10)
    assert squircle_path(box, 0, 5.0) == rounded_rect_path(box, 0)
