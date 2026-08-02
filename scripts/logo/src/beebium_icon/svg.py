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

"""SVG emission.

The document is self-contained: flat fills, one gradient, one optional
shadow filter, and glyphs already reduced to outlines.  Nothing here depends
on a font being installed or on a network fetch.
"""

from __future__ import annotations

from xml.sax.saxutils import escape

from beebium_icon.config import IconConfig, font_for_size
from beebium_icon.layout import IconLayout, build_layout
from beebium_icon.typography import face_for

SVG_NAMESPACE = "http://www.w3.org/2000/svg"


def render_svg(config: IconConfig, size: int) -> str:
    """Render the icon at one size as an SVG document."""
    return layout_to_svg(config, build_layout(config, size))


def layout_to_svg(config: IconConfig, layout: IconLayout) -> str:
    palette = config.palette
    size = layout.size
    frame = layout.frame

    lines = [
        f'<svg xmlns="{SVG_NAMESPACE}" width="{size}" height="{size}" '
        f'viewBox="0 0 {size} {size}">',
        f"<title>{escape(config.content.name)} - {escape(config.content.symbol)}</title>",
        "<defs>",
        '<linearGradient id="face" x1="0" y1="0" x2="0" y2="1">',
        f'<stop offset="0" stop-color="{palette.face_top}"/>',
        f'<stop offset="1" stop-color="{palette.face_bottom}"/>',
        "</linearGradient>",
    ]
    if layout.shadow:
        geometry = config.geometry
        lines.extend(
            [
                '<filter id="shadow" x="-25%" y="-25%" width="150%" height="150%">',
                f'<feDropShadow dx="0" dy="{_n(size * geometry.shadow_dy)}" '
                f'stdDeviation="{_n(size * geometry.shadow_blur)}" '
                f'flood-color="{palette.shadow}" '
                f'flood-opacity="{_n(geometry.shadow_opacity)}"/>',
                "</filter>",
            ]
        )
    lines.append("</defs>")

    body_fill = palette.frame if frame.face_path else "url(#face)"
    shadow_attribute = ' filter="url(#shadow)"' if layout.shadow else ""
    lines.append(f'<path d="{frame.body_path}" fill="{body_fill}"{shadow_attribute}/>')
    if frame.face_path:
        lines.append(f'<path d="{frame.face_path}" fill="url(#face)"/>')

    if frame.keyline_path:
        lines.append(
            f'<path d="{frame.keyline_path}" fill="none" stroke="{palette.ink}" '
            f'stroke-width="{_n(frame.keyline_width)}"/>'
        )

    lines.append(f'<g fill="{palette.ink}">')
    for rule in layout.rules:
        lines.append(
            f'<rect x="{_n(rule.x)}" y="{_n(rule.y)}" width="{_n(rule.width)}" '
            f'height="{_n(rule.height)}"/>'
        )
    for item in layout.texts:
        face = face_for(font_for_size(config, item.role, size))
        data = face.shape(item.text, config.scripts).path_data(item.box, item.align)
        if data:
            lines.append(f'<path d="{data}"/>')
    lines.append("</g>")
    lines.append("</svg>")
    return "\n".join(lines) + "\n"


def _n(value: float) -> str:
    text = f"{value:.3f}".rstrip("0").rstrip(".")
    return "0" if text in ("", "-0") else text
