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

"""Rasterising, and the container formats that hold rasters.

resvg does the rasterising: it is a self-contained Rust renderer shipped as a
wheel, so there is no system Cairo or librsvg to install, and it renders the
same on every platform.  ICNS and ICO are assembled here rather than shelled
out to iconutil, both because iconutil is macOS-only and because every size in
the bundle is rendered at its own level of detail instead of being downsampled
from one large image.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

import resvg_py

from beebium_icon.config import IconConfig
from beebium_icon.svg import render_svg


def render_png(config: IconConfig, size: int) -> bytes:
    """Render the icon at one size as PNG bytes."""
    svg = render_svg(config, size)
    return bytes(resvg_py.svg_to_bytes(svg_string=svg, width=size, height=size))


@dataclass(frozen=True)
class Raster:
    size: int
    png: bytes


def render_rasters(config: IconConfig, sizes: tuple[int, ...]) -> tuple[Raster, ...]:
    """Render each size independently, so each gets its own level of detail."""
    return tuple(Raster(size, render_png(config, size)) for size in sorted(set(sizes)))


# The four-character type each size takes in an ICNS archive.  A size can
# appear twice under different types - once as itself and once as the retina
# variant of a smaller size - which is what iconutil emits.
#
# 16 and 32 go in as icp4 and icp5, the PNG-carrying types; iconutil writes
# those two sizes as RLE-compressed ARGB under ic04 and ic05 instead, which
# buys nothing here.  There is deliberately no icp6: iconutil reads it back as
# 48x48, so 64px travels as ic12 alone.
ICNS_TYPES: dict[int, tuple[str, ...]] = {
    16: ("icp4",),
    32: ("icp5", "ic11"),
    64: ("ic12",),
    128: ("ic07",),
    256: ("ic08", "ic13"),
    512: ("ic09", "ic14"),
    1024: ("ic10",),
}

ICNS_SIZES: tuple[int, ...] = tuple(sorted(ICNS_TYPES))


def build_icns(rasters: tuple[Raster, ...]) -> bytes:
    """Assemble an ICNS archive from PNG-encoded rasters."""
    entries: list[bytes] = []
    for raster in rasters:
        for icns_type in ICNS_TYPES.get(raster.size, ()):
            entries.append(
                icns_type.encode("ascii")
                + struct.pack(">I", len(raster.png) + 8)
                + raster.png
            )
    if not entries:
        raise ValueError(
            f"no ICNS-compatible sizes among {[r.size for r in rasters]}; "
            f"expected some of {list(ICNS_SIZES)}"
        )
    body = b"".join(entries)
    return b"icns" + struct.pack(">I", len(body) + 8) + body


def build_ico(rasters: tuple[Raster, ...]) -> bytes:
    """Assemble a Windows ICO holding PNG-compressed images.

    PNG-compressed entries have been understood since Windows Vista and keep
    each size at the resolution it was rendered at.
    """
    if not rasters:
        raise ValueError("an ICO needs at least one image")
    too_large = [raster.size for raster in rasters if raster.size > 256]
    if too_large:
        raise ValueError(f"ICO images may be at most 256px; got {too_large}")

    count = len(rasters)
    header = struct.pack("<HHH", 0, 1, count)
    directory_length = 16 * count
    offset = len(header) + directory_length
    directory: list[bytes] = []
    for raster in rasters:
        dimension = 0 if raster.size == 256 else raster.size
        directory.append(
            struct.pack(
                "<BBBBHHII",
                dimension,
                dimension,
                0,  # palette size: none, the image is true colour
                0,  # reserved
                1,  # colour planes
                32,  # bits per pixel
                len(raster.png),
                offset,
            )
        )
        offset += len(raster.png)
    return header + b"".join(directory) + b"".join(r.png for r in rasters)
