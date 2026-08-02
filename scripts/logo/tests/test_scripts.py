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

"""Superscript and subscript markup."""

from __future__ import annotations

from dataclasses import replace

import pytest

from beebium_icon.config import SYMBOL, TECHNICAL
from beebium_icon.geometry import Box
from beebium_icon.scripts import (
    NORMAL,
    SUBSCRIPT,
    SUPERSCRIPT,
    ScriptMarkupError,
    Segment,
    parse,
    plain,
)
from beebium_icon.typography import face_for

from tests_support import path_bounds


def test_plain_text_is_one_segment():
    assert parse("16K") == (Segment("16K", NORMAL),)


def test_a_caret_raises_the_next_character():
    assert parse("2^4K") == (
        Segment("2", NORMAL),
        Segment("4", SUPERSCRIPT),
        Segment("K", NORMAL),
    )


def test_an_underscore_lowers_the_next_character():
    assert parse("(6522)_2") == (
        Segment("(6522)", NORMAL),
        Segment("2", SUBSCRIPT),
    )


def test_braces_group_more_than_one_character():
    assert parse("^{128}Bb") == (
        Segment("128", SUPERSCRIPT),
        Segment("Bb", NORMAL),
    )


def test_levels_can_alternate_within_a_run():
    assert parse("^{64}_{6502}Bb") == (
        Segment("64", SUPERSCRIPT),
        Segment("6502", SUBSCRIPT),
        Segment("Bb", NORMAL),
    )


@pytest.mark.parametrize(
    "text, expected", [("2\\^4", "2^4"), ("a\\_b", "a_b"), ("c\\\\d", "c\\d")]
)
def test_a_mark_can_be_escaped(text, expected):
    assert plain(text) == expected
    assert parse(text) == (Segment(expected, NORMAL),)


def test_plain_strips_the_markup():
    assert plain("2^4K · (6522)_2") == "24K · (6522)2"


def test_a_dangling_mark_is_rejected():
    with pytest.raises(ScriptMarkupError, match="nothing to raise or lower"):
        parse("128K^")


def test_an_unclosed_group_is_rejected():
    with pytest.raises(ScriptMarkupError, match="unclosed"):
        parse("^{128")


def test_an_empty_group_is_rejected():
    with pytest.raises(ScriptMarkupError, match="empty group"):
        parse("2^{}")


def test_empty_text_parses_to_one_empty_segment():
    assert parse("") == (Segment(""),)


@pytest.fixture(scope="module")
def technical_face(config):
    return face_for(config.fonts[TECHNICAL])


def test_a_superscript_is_drawn_smaller_than_its_base(technical_face, config):
    base, raised = technical_face.shape("2^4", config.scripts).glyphs
    assert base.scale == 1.0
    assert raised.scale == pytest.approx(config.scripts.scale)


def test_a_superscript_rises_above_the_baseline(technical_face, config):
    box = Box(0, 0, 10_000, 100)
    raised = path_bounds(technical_face.shape("2^4", config.scripts).path_data(box, "left"))
    flat = path_bounds(technical_face.shape("24", config.scripts).path_data(box, "left"))
    # Raised text is taller overall than the same characters on one line.
    assert (raised[3] - raised[1]) > (flat[3] - flat[1])


def test_a_subscript_drops_below_the_baseline(technical_face, config):
    box = Box(0, 0, 10_000, 100)
    lowered = technical_face.shape("H_2", config.scripts)
    flat = technical_face.shape("H2", config.scripts)
    assert lowered.ink.bottom < flat.ink.bottom


def test_a_script_narrows_the_run(technical_face, config):
    """A raised digit is set smaller, so it advances less than a full-size one."""
    assert (
        technical_face.shape("2^4", config.scripts).advance
        < technical_face.shape("24", config.scripts).advance
    )


def test_the_script_scale_is_configurable(technical_face, config):
    small = technical_face.shape("2^4", replace(config.scripts, scale=0.3))
    large = technical_face.shape("2^4", replace(config.scripts, scale=0.9))
    assert small.advance < large.advance


def test_a_scripted_run_still_fits_its_box(config):
    face = face_for(config.fonts[SYMBOL])
    box = Box(50, 60, 300, 140)
    bounds = path_bounds(face.shape("^{32}Bb", config.scripts).path_data(box, "centre"))
    assert box.x - 0.5 <= bounds[0] and bounds[2] <= box.right + 0.5
    assert box.y - 0.5 <= bounds[1] and bounds[3] <= box.bottom + 0.5


def test_markup_reaches_the_rendered_icon(config):
    from beebium_icon.svg import render_svg

    scripted = replace(
        config, content=replace(config.content, isotopes=("2^4", "2^5"), isotope_suffix="K")
    )
    flat = replace(
        config, content=replace(config.content, isotopes=("24", "25"), isotope_suffix="K")
    )
    assert render_svg(scripted, 512) != render_svg(flat, 512)


def test_a_missing_glyph_inside_a_script_names_the_whole_run(config):
    from beebium_icon.typography import TypographyError

    face = face_for(config.fonts[TECHNICAL])
    with pytest.raises(TypographyError, match=r"2\^\{☃\}"):
        face.shape("2^{☃}", config.scripts)
