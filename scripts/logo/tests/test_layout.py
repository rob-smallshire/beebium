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

"""Level of detail and the placement of blocks."""

from __future__ import annotations

from dataclasses import replace

import pytest

from beebium_icon.config import ISOTOPE_NUCLIDE, ISOTOPE_UNIT, SQUIRCLE, TILE
from beebium_icon.scripts import plain
from beebium_icon.layout import (
    KEY_ATOMIC_NUMBER,
    KEY_CONSTITUENTS,
    KEY_GROUND_STATE,
    KEY_ENERGIES,
    KEY_ISOTOPES,
    KEY_KEYLINE,
    KEY_NAME,
    KEY_SYMBOL,
    build_layout,
    energies_text,
    isotopes_text,
    visible_elements,
)


def text_item(layout, key):
    for item in layout.texts:
        if item.key == key:
            return item
    raise AssertionError(f"no {key} in this layout")


def test_the_symbol_survives_every_size(config):
    for size in (16, 32, 64, 128, 256, 512, 1024):
        assert KEY_SYMBOL in visible_elements(config, size)


def test_detail_only_ever_accumulates_with_size(config):
    previous = visible_elements(config, 1)
    for size in range(2, 1025):
        current = visible_elements(config, size)
        assert previous <= current, f"detail was lost between {size - 1} and {size}"
        previous = current


def test_the_smallest_icon_is_the_symbol_alone(config):
    assert visible_elements(config, 16) == {KEY_SYMBOL}


def test_the_largest_icon_shows_everything(config):
    assert visible_elements(config, 1024) == {
        KEY_SYMBOL,
        KEY_KEYLINE,
        KEY_ATOMIC_NUMBER,
        KEY_NAME,
        KEY_ISOTOPES,
        KEY_GROUND_STATE,
        KEY_ENERGIES,
        KEY_CONSTITUENTS,
    }


def test_an_element_with_no_content_never_appears(config):
    without_ground_state = replace(
        config, content=replace(config.content, ground_state="")
    )
    assert KEY_GROUND_STATE not in visible_elements(without_ground_state, 1024)


def test_thresholds_are_inclusive(config):
    threshold = config.lod.isotopes
    assert KEY_ISOTOPES in visible_elements(config, threshold)
    assert KEY_ISOTOPES not in visible_elements(config, threshold - 1)


def test_a_nonpositive_size_is_rejected(config):
    with pytest.raises(ValueError, match="must be positive"):
        build_layout(config, 0)


def test_the_body_is_centred_on_the_canvas(config):
    layout = build_layout(config, 512)
    body = layout.frame.body
    assert body.x == pytest.approx(512 - body.right)
    assert body.y == pytest.approx(512 - body.bottom)
    assert body.width == body.height


@pytest.mark.parametrize("size", [16, 32, 64, 128, 256, 512, 1024])
def test_the_body_edges_fall_on_whole_pixels(config, size):
    body = build_layout(config, size).frame.body
    assert body.x == int(body.x)
    assert body.width == int(body.width)


@pytest.mark.parametrize("size", [128, 256, 512, 1024])
def test_every_block_stays_within_the_face(config, size):
    layout = build_layout(config, size)
    face = layout.frame.face
    for item in layout.texts:
        assert item.box.x >= face.x
        assert item.box.right <= face.right
        assert item.box.y >= face.y
        assert item.box.bottom <= face.bottom


@pytest.mark.parametrize("size", [128, 256, 512, 1024])
def test_blocks_do_not_overlap_and_run_down_the_tile(config, size):
    layout = build_layout(config, size)
    order = [
        KEY_ATOMIC_NUMBER,
        KEY_SYMBOL,
        KEY_NAME,
        KEY_ISOTOPES,
        KEY_ENERGIES,
        KEY_CONSTITUENTS,
    ]
    boxes = [
        text_item(layout, key).box
        for key in order
        if key in {item.key for item in layout.texts}
    ]
    for above, below in zip(boxes, boxes[1:]):
        assert above.bottom <= below.y + 0.01, "blocks overlap"


def test_the_header_pins_the_number_left_and_the_ground_state_right(config):
    layout = build_layout(config, 1024)
    number = text_item(layout, KEY_ATOMIC_NUMBER)
    ground_state = text_item(layout, KEY_GROUND_STATE)
    symbol = text_item(layout, KEY_SYMBOL)
    assert number.box == ground_state.box, "the header shares one strip"
    assert number.align == "left"
    assert ground_state.align == "right"
    assert number.box.bottom <= symbol.box.y


def test_the_symbol_is_capped_so_that_it_cannot_fill_the_tile(config):
    layout = build_layout(config, 1024)
    face = layout.frame.face
    height = text_item(layout, KEY_SYMBOL).box.height
    assert height <= face.width * config.layout.max_symbol_ratio + 0.01


def test_the_symbol_grows_when_it_is_the_only_thing_left(config):
    detailed = build_layout(config, 1024)
    sparse = build_layout(config, 32)
    detailed_share = text_item(detailed, KEY_SYMBOL).box.height / detailed.frame.face.width
    sparse_share = text_item(sparse, KEY_SYMBOL).box.height / sparse.frame.face.width
    assert sparse_share > detailed_share


def test_the_stack_is_centred_in_what_is_left(config):
    layout = build_layout(config, 1024)
    face = layout.frame.face
    flow = [item for item in layout.texts if item.key != KEY_ATOMIC_NUMBER
            and item.key != KEY_GROUND_STATE]
    top = min(item.box.y for item in flow)
    bottom = max(item.box.bottom for item in flow)
    header = text_item(layout, KEY_ATOMIC_NUMBER).box
    above = top - header.bottom
    below = face.bottom - bottom
    assert above > 0 and below > 0


def test_a_rule_precedes_each_ruled_row(config):
    layout = build_layout(config, 1024)
    assert len(layout.rules) == 3
    isotopes = text_item(layout, KEY_ISOTOPES).box
    assert any(rule.bottom <= isotopes.y + 0.01 for rule in layout.rules)


def test_rules_can_be_turned_off(config):
    unruled = replace(
        config,
        layout=replace(
            config.layout,
            isotopes_rule=False,
            energies_rule=False,
            constituents_rule=False,
        ),
    )
    assert build_layout(unruled, 1024).rules == ()


def test_the_squircle_style_has_no_outer_band(config):
    layout = build_layout(config.with_style(SQUIRCLE), 512)
    assert layout.frame.face_path is None
    assert layout.frame.face == layout.frame.body


def test_the_tile_style_has_a_face_inside_a_band(config):
    layout = build_layout(config.with_style(TILE), 512)
    assert layout.frame.face_path is not None
    assert layout.frame.face.width < layout.frame.body.width


def test_the_keyline_is_never_thinner_than_a_pixel(config):
    for size in (16, 48, 64, 1024):
        assert build_layout(config, size).frame.keyline_width >= 1.0


def test_the_shadow_drops_out_at_small_sizes(config):
    assert not build_layout(config, config.geometry.shadow_min_size - 1).shadow
    assert build_layout(config, config.geometry.shadow_min_size).shadow


def test_the_unit_isotope_style_appends_its_unit_without_a_space(config):
    unit = replace(config, content=replace(config.content, isotope_style=ISOTOPE_UNIT))
    text = isotopes_text(unit)
    suffix = unit.content.isotope_suffix
    assert text.startswith(f"{unit.content.isotopes[0]}{suffix}")
    assert f"{suffix} " not in text.replace(unit.content.separator, "")
    assert text.count(suffix) == len(unit.content.isotopes)


def test_the_nuclide_isotope_style_raises_the_size_before_the_symbol(config):
    nuclide = replace(
        config, content=replace(config.content, isotope_style=ISOTOPE_NUCLIDE)
    )
    text = isotopes_text(nuclide)
    for isotope in nuclide.content.isotopes:
        assert f"^{{{isotope}}}{nuclide.content.symbol}" in text
    assert plain(text).startswith(
        f"{nuclide.content.isotopes[0]}{nuclide.content.symbol}"
    )


def test_a_nuclide_row_is_wider_than_a_unit_row(config):
    """Repeating the symbol costs width, which the layout has to absorb."""
    unit = replace(config, content=replace(config.content, isotope_style=ISOTOPE_UNIT))
    nuclide = replace(
        config, content=replace(config.content, isotope_style=ISOTOPE_NUCLIDE)
    )
    assert len(plain(isotopes_text(nuclide))) > len(plain(isotopes_text(unit)))


def test_the_ground_state_does_not_repeat_the_energy_levels(config):
    """The top-right slot said "2 MHz" while the bottom row said "2 . 3 . 4 MHz"."""
    assert config.content.ground_state not in energies_text(config)


def test_the_energy_row_carries_its_unit(config):
    assert energies_text(config).endswith(config.content.energy_suffix)
    assert energies_text(config).startswith(config.content.energies[0])
