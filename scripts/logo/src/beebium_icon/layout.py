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

"""Level of detail, and where everything sits on the tile.

The tile is laid out for one rendered size at a time.  Which annotations
appear is decided first, from the level-of-detail thresholds; the surviving
blocks are then apportioned the height between them and the stack is centred,
so a 64px icon showing only an atomic number and a symbol is composed as
deliberately as a 1024px icon showing everything.
"""

from __future__ import annotations

from dataclasses import dataclass

from beebium_icon.config import (
    ISOTOPE_NUCLIDE,
    NAME,
    SQUIRCLE,
    SYMBOL,
    TECHNICAL,
    IconConfig,
)
from beebium_icon.geometry import Box, rounded_rect_path, square, squircle_path

# Layout element keys.
KEY_SYMBOL = "symbol"
KEY_NAME = "name"
KEY_ATOMIC_NUMBER = "atomic_number"
KEY_GROUND_STATE = "ground_state"
KEY_ISOTOPES = "isotopes"
KEY_ENERGIES = "energies"
KEY_CONSTITUENTS = "constituents"
KEY_KEYLINE = "keyline"


@dataclass(frozen=True)
class TextItem:
    """A string to be set into a box."""

    key: str
    role: str
    text: str
    box: Box
    align: str


@dataclass(frozen=True)
class Frame:
    """The tile itself: an outer band, a face, and a keyline."""

    body: Box
    face: Box
    body_path: str
    face_path: str | None
    keyline_path: str | None
    keyline_width: float


@dataclass(frozen=True)
class IconLayout:
    """Everything needed to draw one icon at one size."""

    size: int
    frame: Frame
    texts: tuple[TextItem, ...]
    rules: tuple[Box, ...]
    shadow: bool
    elements: frozenset[str]


def visible_elements(config: IconConfig, size: int) -> frozenset[str]:
    """The elements that survive the level-of-detail thresholds at this size."""
    content = config.content
    lod = config.lod
    candidates = {
        KEY_SYMBOL: (lod.symbol, content.symbol),
        KEY_KEYLINE: (lod.keyline, "-"),
        KEY_ATOMIC_NUMBER: (lod.atomic_number, content.atomic_number),
        KEY_NAME: (lod.name, content.name),
        KEY_ISOTOPES: (lod.isotopes, _join(content.isotopes, content.separator)),
        KEY_GROUND_STATE: (lod.ground_state, content.ground_state),
        KEY_ENERGIES: (lod.energies, _join(content.energies, content.separator)),
        KEY_CONSTITUENTS: (
            lod.constituents,
            _join(content.constituents, content.separator),
        ),
    }
    return frozenset(
        key
        for key, (threshold, text) in candidates.items()
        if text and size >= threshold
    )


def _join(parts: tuple[str, ...], separator: str) -> str:
    return separator.join(parts)


def isotopes_text(config: IconConfig) -> str:
    content = config.content
    if content.isotope_style == ISOTOPE_NUCLIDE:
        # As chemistry writes an isotope: the mass number raised before the
        # symbol, the size of the machine's memory standing in for it.
        parts = tuple(f"^{{{isotope}}}{content.symbol}" for isotope in content.isotopes)
    else:
        parts = tuple(f"{isotope}{content.isotope_suffix}" for isotope in content.isotopes)
    return _join(parts, content.separator)


def constituents_text(config: IconConfig) -> str:
    return _join(config.content.constituents, config.content.separator)


def energies_text(config: IconConfig) -> str:
    text = _join(config.content.energies, config.content.separator)
    suffix = config.content.energy_suffix
    return f"{text} {suffix}" if text and suffix else text


def build_layout(config: IconConfig, size: int) -> IconLayout:
    """Lay the tile out at a given rendered size, in device pixels."""
    if size <= 0:
        raise ValueError(f"icon size must be positive, got {size}")
    geometry = config.geometry
    layout = config.layout
    elements = visible_elements(config, size)

    is_squircle = geometry.style == SQUIRCLE
    shadow = size >= geometry.shadow_min_size and geometry.shadow_opacity > 0
    if not shadow:
        body_ratio = geometry.unshadowed_body_ratio
    else:
        body_ratio = geometry.body_ratio if is_squircle else geometry.tile_body_ratio
    # An even margin puts the body edges on whole pixels, which matters at
    # 16px where a half-pixel edge is a grey smear round the whole tile.
    side = round(size * body_ratio)
    if (size - side) % 2:
        side -= 1
    body = square(side, size)
    corner = side * geometry.corner_ratio

    def outline(box: Box, radius: float) -> str:
        if is_squircle:
            return squircle_path(box, radius, geometry.squircle_exponent)
        return rounded_rect_path(box, radius)

    body_path = outline(body, corner)
    if is_squircle:
        # The macOS shape is a single continuous body; there is no outer band.
        face = body
        face_path = None
    else:
        band = side * geometry.frame_ratio
        face = body.inset(band)
        face_path = outline(face, max(0.0, corner - band))

    # Everything inside the tile is proportional to the face, not to the body,
    # so that the tile style's outer band does not squeeze the content.
    face_side = face.width
    keyline_width = max(1.0, face_side * geometry.keyline_width)
    if KEY_KEYLINE in elements:
        keyline_box = face.inset(face_side * geometry.keyline_inset + keyline_width * 0.5)
        keyline_path = outline(keyline_box, face_side * geometry.keyline_corner_ratio)
    else:
        keyline_path = None

    frame = Frame(
        body=body,
        face=face,
        body_path=body_path,
        face_path=face_path,
        keyline_path=keyline_path,
        keyline_width=keyline_width,
    )

    inset = geometry.content_inset if keyline_path else geometry.bare_content_inset
    content_box = face.inset(face_side * inset)
    texts: list[TextItem] = []
    rules: list[Box] = []

    header_shown = bool(elements & {KEY_ATOMIC_NUMBER, KEY_GROUND_STATE})
    flow = content_box
    if header_shown:
        header_height = face_side * layout.header_height
        strip, flow = content_box.slice_top(header_height * (1.0 + layout.header_gap))
        header = Box(strip.x, strip.y, strip.width, header_height)
        if KEY_ATOMIC_NUMBER in elements:
            texts.append(
                TextItem(
                    KEY_ATOMIC_NUMBER,
                    TECHNICAL,
                    config.content.atomic_number,
                    header,
                    "left",
                )
            )
        if KEY_GROUND_STATE in elements:
            texts.append(
                TextItem(
                    KEY_GROUND_STATE,
                    TECHNICAL,
                    config.content.ground_state,
                    header,
                    "right",
                )
            )

    blocks: list[tuple[str, str, str, float, float, bool]] = [
        (KEY_SYMBOL, SYMBOL, config.content.symbol, layout.symbol_weight, 0.0, False)
    ]
    if KEY_NAME in elements:
        blocks.append(
            (KEY_NAME, NAME, config.content.name, layout.name_weight, layout.name_gap, False)
        )
    if KEY_ISOTOPES in elements:
        blocks.append(
            (
                KEY_ISOTOPES,
                TECHNICAL,
                isotopes_text(config),
                layout.isotopes_weight,
                layout.isotopes_gap,
                layout.isotopes_rule,
            )
        )
    if KEY_ENERGIES in elements:
        blocks.append(
            (
                KEY_ENERGIES,
                TECHNICAL,
                energies_text(config),
                layout.energies_weight,
                layout.energies_gap,
                layout.energies_rule,
            )
        )
    if KEY_CONSTITUENTS in elements:
        blocks.append(
            (
                KEY_CONSTITUENTS,
                TECHNICAL,
                constituents_text(config),
                layout.constituents_weight,
                layout.constituents_gap,
                layout.constituents_rule,
            )
        )

    units = sum(weight + gap for _, _, _, weight, gap, _ in blocks)
    unit = flow.height / units if units else 0.0
    symbol_cap = face_side * (
        layout.solo_symbol_ratio
        if len(blocks) == 1 and not header_shown
        else layout.max_symbol_ratio
    )
    heights = [
        min(weight * unit, symbol_cap) if key == KEY_SYMBOL else weight * unit
        for key, _, _, weight, _, _ in blocks
    ]
    gaps = [gap * unit for _, _, _, _, gap, _ in blocks]
    used = sum(heights) + sum(gaps)

    y = flow.y + (flow.height - used) * 0.5
    rule_width = max(1.0, face_side * layout.rule_width)
    for index, (key, role, text, _, _, rule) in enumerate(blocks):
        gap = gaps[index]
        if rule and gap > 0:
            rule_x = content_box.x + face_side * layout.rule_inset
            rules.append(
                Box(
                    rule_x,
                    y + gap * 0.5 - rule_width * 0.5,
                    content_box.width - 2 * face_side * layout.rule_inset,
                    rule_width,
                )
            )
        y += gap
        texts.append(
            TextItem(key, role, text, Box(content_box.x, y, content_box.width, heights[index]), "centre")
        )
        y += heights[index]

    return IconLayout(
        size=size,
        frame=frame,
        texts=tuple(texts),
        rules=tuple(rules),
        shadow=shadow,
        elements=elements,
    )
