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

"""Shared test support: asset paths and helpers for inspecting path data.

A uniquely named module (not conftest.py) for symbols shared across test
modules. Importing a conftest.py by its bare name is fragile -- when several
conftest.py files are collected together pytest can bind the name `conftest` to
the wrong one -- so shared constants and helpers live here instead.
"""

from __future__ import annotations

import re
from pathlib import Path

# Repo asset paths, shared by the config tests and the config fixture.
LOGO_DIRPATH = Path(__file__).resolve().parents[1]
CONFIGS_DIRPATH = LOGO_DIRPATH / "configs"
DEFAULT_CONFIG_FILEPATH = CONFIGS_DIRPATH / "beebium.toml"
ARTWORK_FILEPATH = LOGO_DIRPATH / "the-shape-of-beebium.png"

TOKEN = re.compile(r"([MLHVCQAZ])|(-?\d+(?:\.\d+)?)")
CURVE_SAMPLES = 24


def outline_points(path_data: str) -> list[tuple[float, float]]:
    """Flatten path data to points lying on the outline.

    Curves are sampled rather than taken at their control points, which would
    overstate the bounds: a control point can sit well outside the outline.
    Arcs contribute their endpoints only, which is enough for bounds, because
    an arc drawn here always bulges towards a corner the straight edges
    already reach.
    """
    points: list[tuple[float, float]] = []
    position = (0.0, 0.0)
    start = (0.0, 0.0)
    for verb, values in _commands(path_data):
        if verb == "M":
            position = start = (values[0], values[1])
            points.append(position)
        elif verb == "L":
            position = (values[0], values[1])
            points.append(position)
        elif verb == "H":
            position = (values[0], position[1])
            points.append(position)
        elif verb == "V":
            position = (position[0], values[0])
            points.append(position)
        elif verb == "A":
            position = (values[-2], values[-1])
            points.append(position)
        elif verb == "Q":
            control = (values[0], values[1])
            end = (values[2], values[3])
            points.extend(_sample(position, control, control, end))
            position = end
        elif verb == "C":
            end = (values[4], values[5])
            points.extend(
                _sample(position, (values[0], values[1]), (values[2], values[3]), end)
            )
            position = end
        elif verb == "Z":
            position = start
    return points


def _commands(path_data: str) -> list[tuple[str, list[float]]]:
    commands: list[tuple[str, list[float]]] = []
    current: tuple[str, list[float]] | None = None
    for match in TOKEN.finditer(path_data):
        verb, number = match.groups()
        if verb:
            current = (verb, [])
            commands.append(current)
        elif current is not None:
            current[1].append(float(number))
    return commands


def _sample(p0, p1, p2, p3) -> list[tuple[float, float]]:
    sampled = []
    for step in range(1, CURVE_SAMPLES + 1):
        t = step / CURVE_SAMPLES
        u = 1.0 - t
        sampled.append(
            (
                u**3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t**3 * p3[0],
                u**3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t**3 * p3[1],
            )
        )
    return sampled


def path_bounds(path_data: str) -> tuple[float, float, float, float]:
    """The bounding box of path data as (left, top, right, bottom)."""
    points = outline_points(path_data)
    if not points:
        raise ValueError("path data encloses nothing")
    xs = [x for x, _ in points]
    ys = [y for _, y in points]
    return min(xs), min(ys), max(xs), max(ys)
