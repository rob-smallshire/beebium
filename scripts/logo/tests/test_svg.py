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

"""SVG emission."""

from __future__ import annotations

import xml.etree.ElementTree as ElementTree
from dataclasses import replace

import pytest

from beebium_icon.config import TILE
from beebium_icon.svg import SVG_NAMESPACE, render_svg


def parse(svg: str) -> ElementTree.Element:
    return ElementTree.fromstring(svg)


@pytest.mark.parametrize("size", [16, 64, 512, 1024])
def test_the_document_is_wellformed_and_the_right_size(config, size):
    root = parse(render_svg(config, size))
    assert root.tag == f"{{{SVG_NAMESPACE}}}svg"
    assert root.get("width") == str(size)
    assert root.get("viewBox") == f"0 0 {size} {size}"


def test_no_text_element_survives_into_the_output(config):
    """Text is outlined at generation time; a live <text> would need a font."""
    root = parse(render_svg(config, 1024))
    assert root.findall(f".//{{{SVG_NAMESPACE}}}text") == []
    assert "font-family" not in render_svg(config, 1024)


def test_the_ink_colour_is_the_configured_one(config):
    svg = render_svg(config, 512)
    assert config.palette.ink in svg


def test_rendering_is_deterministic(config):
    assert render_svg(config, 256) == render_svg(config, 256)


def test_more_paths_are_drawn_as_detail_arrives(config):
    def path_count(size: int) -> int:
        return len(parse(render_svg(config, size)).findall(f".//{{{SVG_NAMESPACE}}}path"))

    assert path_count(16) < path_count(128) < path_count(1024)


def test_the_shadow_filter_appears_only_when_the_shadow_does(config):
    assert "feDropShadow" not in render_svg(config, 32)
    assert "feDropShadow" in render_svg(config, 512)


def test_the_tile_style_paints_a_band_and_a_face(config):
    root = parse(render_svg(config.with_style(TILE), 512))
    fills = [path.get("fill") for path in root.findall(f"{{{SVG_NAMESPACE}}}path")]
    assert config.palette.frame in fills
    assert "url(#face)" in fills


def test_content_reaches_the_output(config):
    """A different symbol must change the drawing, not just the title."""
    other = replace(config, content=replace(config.content, symbol="Zz"))
    assert render_svg(other, 512) != render_svg(config, 512)


def test_the_title_names_the_element(config):
    root = parse(render_svg(config, 512))
    title = root.find(f"{{{SVG_NAMESPACE}}}title")
    assert title is not None
    assert config.content.name in title.text
