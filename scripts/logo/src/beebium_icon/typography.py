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

"""Text as outlines.

Every string on the tile is shaped with HarfBuzz and converted to SVG path
data at generation time, so a rendered icon depends on no installed font and
comes out identically on any machine.  Coordinates arrive from the font in
font units with y pointing up, and leave in device pixels with y pointing
down.
"""

from __future__ import annotations

import functools
from dataclasses import dataclass
from pathlib import Path

import uharfbuzz as hb

from beebium_icon.config import FIT_CAP, FontRole, Scripts
from beebium_icon.geometry import Box
from beebium_icon.scripts import SUBSCRIPT, SUPERSCRIPT
from beebium_icon.scripts import parse as parse_scripts


class TypographyError(ValueError):
    """Raised when a font cannot render the text asked of it."""


@dataclass(frozen=True)
class Ink:
    """The inked bounding box of a run, in font units, y up, baseline at 0."""

    left: float
    bottom: float
    right: float
    top: float

    @property
    def width(self) -> float:
        return self.right - self.left

    @property
    def height(self) -> float:
        return self.top - self.bottom


@dataclass(frozen=True)
class PlacedGlyph:
    """A glyph on the run's baseline, in font units.

    A raised or lowered glyph carries its own scale and sits off the baseline,
    so one run can mix levels the way a chemical formula does.
    """

    glyph_id: int
    x: float
    y: float
    scale: float = 1.0


class Face:
    """A font at a fixed weight, ready to shape and draw."""

    def __init__(self, role: FontRole) -> None:
        self._role = role
        blob = hb.Blob.from_file_path(str(role.filepath))
        self._face = hb.Face(blob, role.face_index)
        self._font = hb.Font(self._face)
        if role.variations:
            self._font.set_variations({k: float(v) for k, v in role.variations.items()})
        self._draw_funcs = _make_draw_funcs()

    @property
    def upem(self) -> int:
        return self._face.upem

    @property
    def tracking(self) -> float:
        return self._role.tracking

    @property
    def fit(self) -> str:
        return self._role.fit

    @functools.cached_property
    def cap_height(self) -> float:
        """Height of a capital H in font units, used to size technical text.

        Measuring the glyph rather than trusting the OS/2 table keeps runs of
        digits and capitals the same size across faces whose metrics disagree.
        """
        run = self.shape("H")
        return run.ink.height if run.ink else self.upem * 0.7

    def shape(self, text: str, scripts: Scripts | None = None) -> TextRun:
        """Shape marked-up text into positioned glyphs at the origin baseline.

        Segments are shaped one at a time, so kerning does not cross the join
        between a base and its script - which is what you want, since the two
        are set at different sizes anyway.
        """
        scripts = scripts or Scripts()
        tracking = self._role.tracking * self.upem
        glyphs: list[PlacedGlyph] = []
        pen_x = 0.0
        for segment in parse_scripts(text):
            if not segment.text:
                continue
            scale, rise = self._level(segment.level, scripts)
            for glyph_id, offset_x, offset_y, advance in self._shape_plainly(
                segment.text, text
            ):
                glyphs.append(
                    PlacedGlyph(
                        glyph_id,
                        pen_x + offset_x * scale,
                        rise + offset_y * scale,
                        scale,
                    )
                )
                pen_x += advance * scale + tracking
        # Tracking after the final glyph is not part of the run.
        advance = pen_x - tracking if glyphs else 0.0
        return TextRun(self, tuple(glyphs), advance, self._ink(glyphs))

    def _level(self, level: str, scripts: Scripts) -> tuple[float, float]:
        if level == SUPERSCRIPT:
            return scripts.scale, scripts.superscript_rise * self.upem
        if level == SUBSCRIPT:
            return scripts.scale, -scripts.subscript_drop * self.upem
        return 1.0, 0.0

    def _shape_plainly(self, text: str, whole: str):
        buffer = hb.Buffer()
        buffer.add_str(text)
        buffer.guess_segment_properties()
        hb.shape(self._font, buffer)

        missing = sorted(
            {text[info.cluster] for info in buffer.glyph_infos if info.codepoint == 0}
        )
        if missing:
            described = ", ".join(f"{c!r} (U+{ord(c):04X})" for c in missing)
            raise TypographyError(
                f"{self._role.filepath.name} has no glyph for {described}, "
                f"needed by {whole!r}"
            )
        return [
            (info.codepoint, position.x_offset, position.y_offset, position.x_advance)
            for info, position in zip(buffer.glyph_infos, buffer.glyph_positions)
        ]

    def _ink(self, glyphs: list[PlacedGlyph]) -> Ink | None:
        boxes = []
        for glyph in glyphs:
            extents = self._font.get_glyph_extents(glyph.glyph_id)
            if extents is None or extents.width == 0 or extents.height == 0:
                continue  # A space, or another glyph with no outline.
            left = glyph.x + extents.x_bearing * glyph.scale
            top = glyph.y + extents.y_bearing * glyph.scale
            boxes.append(
                (
                    left,
                    top + extents.height * glyph.scale,
                    left + extents.width * glyph.scale,
                    top,
                )
            )
        if not boxes:
            return None
        return Ink(
            left=min(box[0] for box in boxes),
            bottom=min(box[1] for box in boxes),
            right=max(box[2] for box in boxes),
            top=max(box[3] for box in boxes),
        )

    def outline(self, glyph_id: int) -> list[tuple]:
        """Draw one glyph, returning path commands in font units, y up."""
        commands: list[tuple] = []
        self._font.draw_glyph(glyph_id, self._draw_funcs, commands)
        return commands


def _make_draw_funcs() -> hb.DrawFuncs:
    funcs = hb.DrawFuncs()
    funcs.set_move_to_func(lambda x, y, out: out.append(("M", x, y)))
    funcs.set_line_to_func(lambda x, y, out: out.append(("L", x, y)))
    funcs.set_quadratic_to_func(
        lambda cx, cy, x, y, out: out.append(("Q", cx, cy, x, y))
    )
    funcs.set_cubic_to_func(
        lambda c1x, c1y, c2x, c2y, x, y, out: out.append(("C", c1x, c1y, c2x, c2y, x, y))
    )
    funcs.set_close_path_func(lambda out: out.append(("Z",)))
    return funcs


@dataclass(frozen=True)
class TextRun:
    """A shaped string, positioned on a baseline at the origin, in font units."""

    face: Face
    glyphs: tuple[PlacedGlyph, ...]
    advance: float
    ink: Ink | None

    def scale_to(self, box: Box) -> float:
        """The scale at which this run fills box without overflowing it."""
        if self.ink is None or self.ink.height <= 0:
            return 0.0
        reference = self.face.cap_height if self.face.fit == FIT_CAP else self.ink.height
        scale = box.height / reference
        if self.ink.width * scale > box.width:
            scale = box.width / self.ink.width
        return scale

    def path_data(self, box: Box, align: str = "centre") -> str:
        """Render this run as SVG path data, fitted into box in device pixels.

        The run is scaled to the box, then positioned by its inked bounds
        rather than its advance width, which centres text optically.
        """
        scale = self.scale_to(box)
        if scale == 0.0 or self.ink is None:
            return ""
        if align == "left":
            origin_x = box.x - self.ink.left * scale
        elif align == "right":
            origin_x = box.right - self.ink.right * scale
        elif align == "centre":
            origin_x = box.centre_x - (self.ink.left + self.ink.right) * 0.5 * scale
        else:
            raise TypographyError(f"unknown alignment {align!r}")
        # y grows downwards on the canvas and upwards in the font.
        origin_y = box.centre_y + (self.ink.bottom + self.ink.top) * 0.5 * scale
        return self.transformed_path_data(origin_x, origin_y, scale)

    def transformed_path_data(self, origin_x: float, origin_y: float, scale: float) -> str:
        parts: list[str] = []
        for glyph in self.glyphs:
            for command in self.face.outline(glyph.glyph_id):
                verb = command[0]
                if verb == "Z":
                    parts.append("Z")
                    continue
                coordinates = []
                for index in range(1, len(command), 2):
                    x = origin_x + (glyph.x + command[index] * glyph.scale) * scale
                    y = origin_y - (glyph.y + command[index + 1] * glyph.scale) * scale
                    coordinates.append(f"{_round(x)} {_round(y)}")
                parts.append(verb + " ".join(coordinates))
        return " ".join(parts)


def _round(value: float) -> str:
    text = f"{value:.2f}".rstrip("0").rstrip(".")
    return "0" if text in ("", "-0") else text


@functools.lru_cache(maxsize=None)
def _cached_face(filepath: Path, face_index: int, variations: tuple, tracking: float, fit: str) -> Face:
    return Face(
        FontRole(
            filepath=filepath,
            face_index=face_index,
            variations=dict(variations),
            tracking=tracking,
            fit=fit,
        )
    )


def face_for(role: FontRole) -> Face:
    """Return a cached Face for a role; loading a font is not cheap."""
    return _cached_face(
        role.filepath,
        role.face_index,
        tuple(sorted(role.variations.items())),
        role.tracking,
        role.fit,
    )
