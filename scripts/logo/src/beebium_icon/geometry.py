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

"""Boxes and outlines.

All coordinates here are device pixels for the icon being rendered, with x to
the right and y downwards, matching SVG.  Working in device pixels rather than
in a nominal 1024-unit space lets small sizes snap features to the pixel grid,
which is the difference between a crisp 16px keyline and a grey smear.
"""

from __future__ import annotations

import math
from dataclasses import dataclass


@dataclass(frozen=True)
class Box:
    """An axis-aligned rectangle."""

    x: float
    y: float
    width: float
    height: float

    @property
    def right(self) -> float:
        return self.x + self.width

    @property
    def bottom(self) -> float:
        return self.y + self.height

    @property
    def centre_x(self) -> float:
        return self.x + self.width * 0.5

    @property
    def centre_y(self) -> float:
        return self.y + self.height * 0.5

    def inset(self, amount: float) -> Box:
        return Box(
            self.x + amount,
            self.y + amount,
            self.width - 2 * amount,
            self.height - 2 * amount,
        )

    def slice_top(self, height: float) -> tuple[Box, Box]:
        """Split off a strip of the given height from the top."""
        height = min(height, self.height)
        top = Box(self.x, self.y, self.width, height)
        rest = Box(self.x, self.y + height, self.width, self.height - height)
        return top, rest


def square(side: float, canvas: float) -> Box:
    """A square of the given side, centred on a square canvas."""
    origin = (canvas - side) * 0.5
    return Box(origin, origin, side, side)


def rounded_rect_path(box: Box, radius: float) -> str:
    """SVG path data for a rectangle with circular corners."""
    radius = max(0.0, min(radius, box.width * 0.5, box.height * 0.5))
    x, y, w, h = box.x, box.y, box.width, box.height
    r = radius
    return (
        f"M{_n(x + r)} {_n(y)}"
        f"H{_n(x + w - r)}A{_n(r)} {_n(r)} 0 0 1 {_n(x + w)} {_n(y + r)}"
        f"V{_n(y + h - r)}A{_n(r)} {_n(r)} 0 0 1 {_n(x + w - r)} {_n(y + h)}"
        f"H{_n(x + r)}A{_n(r)} {_n(r)} 0 0 1 {_n(x)} {_n(y + h - r)}"
        f"V{_n(y + r)}A{_n(r)} {_n(r)} 0 0 1 {_n(x + r)} {_n(y)}Z"
    )


def squircle_path(box: Box, radius: float, exponent: float, segments: int = 16) -> str:
    """SVG path data for a superellipse-cornered rectangle.

    A rounded rectangle meets its straight edges with a curvature step, which
    reads as a pinch at icon sizes.  A superellipse corner has continuous
    curvature, which is why Apple's icon shape uses one.  The corner is sampled
    and fitted with cubics; at exponent 2 this degenerates to a circular corner
    and the result matches rounded_rect_path to within a fraction of a pixel.
    """
    radius = max(0.0, min(radius, box.width * 0.5, box.height * 0.5))
    if radius == 0.0:
        return rounded_rect_path(box, 0.0)
    exponent = max(2.0, exponent)

    # A unit quarter of the superellipse, from the horizontal axis round to the
    # vertical one.  Each corner is this quarter, reflected into place.
    power = 2.0 / exponent
    quarter = []
    for step in range(segments + 1):
        theta = (math.pi * 0.5) * step / segments
        quarter.append((math.cos(theta) ** power, math.sin(theta) ** power))

    left = box.x + radius
    top = box.y + radius
    right = box.right - radius
    bottom = box.bottom - radius

    # Walk clockwise, so that each corner ends where the next straight edge
    # begins and the edges fall out of the gaps between corners.
    points: list[tuple[float, float]] = []
    for cx, cy in reversed(quarter):  # top right
        points.append((right + radius * cx, top - radius * cy))
    for cx, cy in quarter:  # bottom right
        points.append((right + radius * cx, bottom + radius * cy))
    for cx, cy in reversed(quarter):  # bottom left
        points.append((left - radius * cx, bottom + radius * cy))
    for cx, cy in quarter:  # top left
        points.append((left - radius * cx, top - radius * cy))
    return _closed_smooth_path(points)


def _closed_smooth_path(points: list[tuple[float, float]]) -> str:
    """Fit cubics through a closed sequence of points.

    The knots are spaced centripetally rather than uniformly.  Corner samples
    crowd together while the straight edges between them are long, and a
    uniform parameterisation answers that mismatch with control handles that
    overshoot, putting a visible tick at every corner of a nearly square
    outline.
    """
    points = _without_repeats(points)
    count = len(points)
    parts = [f"M{_n(points[0][0])} {_n(points[0][1])}"]
    for index in range(count):
        p0 = points[(index - 1) % count]
        p1 = points[index]
        p2 = points[(index + 1) % count]
        p3 = points[(index + 2) % count]
        c1, c2 = _handles(p0, p1, p2, p3)
        parts.append(
            f"C{_n(c1[0])} {_n(c1[1])} {_n(c2[0])} {_n(c2[1])} {_n(p2[0])} {_n(p2[1])}"
        )
    parts.append("Z")
    return "".join(parts)


def _handles(p0, p1, p2, p3):
    """The two control points of the cubic running from p1 to p2."""
    d1 = math.dist(p0, p1) ** 0.5
    d2 = math.dist(p1, p2) ** 0.5
    d3 = math.dist(p2, p3) ** 0.5
    if d1 <= 0.0 or d2 <= 0.0 or d3 <= 0.0:
        # Coincident knots: fall back to the uniform tangent estimate.
        return (
            (p1[0] + (p2[0] - p0[0]) / 6.0, p1[1] + (p2[1] - p0[1]) / 6.0),
            (p2[0] - (p3[0] - p1[0]) / 6.0, p2[1] - (p3[1] - p1[1]) / 6.0),
        )
    c1 = []
    c2 = []
    for axis in (0, 1):
        a, b, c, d = p0[axis], p1[axis], p2[axis], p3[axis]
        outgoing = d2 * ((b - a) / d1 - (c - a) / (d1 + d2) + (c - b) / d2)
        incoming = d2 * ((c - b) / d2 - (d - b) / (d2 + d3) + (d - c) / d3)
        c1.append(b + outgoing / 3.0)
        c2.append(c - incoming / 3.0)
    return tuple(c1), tuple(c2)


def _without_repeats(points: list[tuple[float, float]]) -> list[tuple[float, float]]:
    """Drop knots that coincide with their neighbour, as a tiny radius makes."""
    kept = [points[0]]
    for point in points[1:]:
        if math.dist(point, kept[-1]) > 1e-9:
            kept.append(point)
    if len(kept) > 2 and math.dist(kept[0], kept[-1]) <= 1e-9:
        kept.pop()
    return kept


def _n(value: float) -> str:
    text = f"{value:.2f}".rstrip("0").rstrip(".")
    return "0" if text in ("", "-0") else text
